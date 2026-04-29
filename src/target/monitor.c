/*
 * Monitor Target Implementation
 *
 * A Monitor represents a physical display (RandR output). It owns:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Adopted state machines
 * - Tag state and layout configuration
 *
 * Note: Client associations are decoupled - use hub or client_list
 * to query clients by monitor, rather than storing Client pointers.
 */

#include <stdlib.h>
#include <string.h>

#include "../../wm-hub.h"
#include "../../wm-log.h"
#include "monitor.h"

/*
 * Monitor list
 */
static Monitor* monitor_list      = NULL;
Monitor*        _monitor_selected = NULL;

/*
 * Initialize the monitor list.
 * Clears any stale monitors from previous runs.
 */
void
monitor_list_init(void)
{
  /* Clear any stale monitors from previous runs */
  while (monitor_list != NULL) {
    Monitor* next = monitor_list->next;
    /* Only free if not registered with hub (orphaned) */
    if (!monitor_list->target.registered) {
      free(monitor_list);
    }
    monitor_list = next;
  }
  monitor_list      = NULL;
  _monitor_selected = NULL;
}

/*
 * Shutdown the monitor list.
 * Destroys all monitors in the list.
 */
void
monitor_list_shutdown(void)
{
  while (monitor_list != NULL) {
    Monitor* next = monitor_list->next;
    /* Unregister from hub if still registered */
    if (monitor_list->target.registered) {
      hub_unregister_target(monitor_list->target.id);
    }
    free(monitor_list);
    monitor_list = next;
  }
  monitor_list      = NULL;
  _monitor_selected = NULL;
}

/*
 * Create a new Monitor for a RandR output.
 */
Monitor*
monitor_create(xcb_randr_output_t output)
{
  if (output == XCB_NONE) {
    LOG_ERROR("Cannot create monitor for invalid output: %u", output);
    return NULL;
  }

  /* Check for duplicate output ID */
  if (hub_get_target_by_id((TargetID) output) != NULL) {
    LOG_ERROR("Monitor with output %u already registered", output);
    return NULL;
  }

  LOG_DEBUG("Creating monitor for output: %u", output);

  Monitor* m = malloc(sizeof(Monitor));
  if (m == NULL) {
    LOG_ERROR("Failed to allocate Monitor");
    return NULL;
  }

  /* Initialize base target */
  m->target.id         = (TargetID) output;
  m->target.type       = TARGET_TYPE_MONITOR;
  m->target.registered = false;

  /* Initialize RandR properties */
  m->output = output;
  m->crtc   = XCB_NONE;

  /* Initialize geometry */
  m->x      = 0;
  m->y      = 0;
  m->width  = 0;
  m->height = 0;

  /* Initialize tag state */
  m->tagset     = MONITOR_TAG_MASK(0); /* tag 0 */
  m->prevtagset = MONITOR_TAG_MASK(0);

  /* Initialize layout configuration */
  m->mfact   = 0.5F;
  m->nmaster = 1;

  /* Initialize client associations (decoupled - use hub to query) */
  m->client_count = 0;
  m->sel_window   = XCB_NONE;
  m->stack_head   = XCB_NONE;

  /* Initialize bar */
  m->bar = NULL;

  /* Add to list */
  m->next      = monitor_list;
  monitor_list = m;

  /* Always select the most recently added monitor */
  _monitor_selected = m;

  /* Register with Hub */
  hub_register_target(&m->target);

  /* Verify registration succeeded */
  if (!m->target.registered) {
    LOG_ERROR("Failed to register monitor target: output=%u", output);
    /* Remove from list */
    Monitor** prev = &monitor_list;
    while (*prev != NULL && *prev != m) {
      prev = &(*prev)->next;
    }
    if (*prev == m) {
      *prev = m->next;
    }
    free(m);
    return NULL;
  }

  LOG_DEBUG("Monitor created: output=%u", output);
  return m;
}

/*
 * Destroy a Monitor.
 */
void
monitor_destroy(Monitor* m)
{
  if (m == NULL)
    return;

  LOG_DEBUG("Destroying monitor: output=%u", m->output);

  /* Remove from list */
  Monitor** prev = &monitor_list;
  while (*prev != NULL && *prev != m) {
    prev = &(*prev)->next;
  }
  if (*prev == m) {
    *prev = m->next;
  }

  /* Clear client associations (clients detach via client_destroy) */
  m->client_count = 0;
  m->sel_window   = XCB_NONE;
  m->stack_head   = XCB_NONE;

  /* If this was the selected monitor, select the first remaining */
  if (_monitor_selected == m) {
    _monitor_selected = monitor_list_get_first();
  }

  /* Unregister from Hub */
  if (m->target.registered) {
    hub_unregister_target(m->target.id);
  }

  /* Free monitor */
  free(m);

  LOG_DEBUG("Monitor destroyed");
}

/*
 * Get the first monitor in the global list.
 */
Monitor*
monitor_list_get_first(void)
{
  return monitor_list;
}

/*
 * Get the next monitor after the given one.
 */
Monitor*
monitor_list_get_next(Monitor* m)
{
  if (m == NULL)
    return NULL;
  return m->next;
}

/*
 * Add a monitor to the global list.
 */
void
monitor_list_add(Monitor* m)
{
  if (m == NULL)
    return;

  m->next      = monitor_list;
  monitor_list = m;

  /* Always select the most recently added monitor */
  _monitor_selected = m;
}

/*
 * Remove a monitor from the global list.
 */
void
monitor_list_remove(Monitor* m)
{
  if (m == NULL)
    return;

  /* Remove from linked list */
  if (monitor_list == m) {
    monitor_list = m->next;
  } else {
    Monitor* prev = monitor_list;
    while (prev != NULL && prev->next != m) {
      prev = prev->next;
    }
    if (prev != NULL) {
      prev->next = m->next;
    }
  }

  m->next = NULL;

  /* If removed selected monitor, select first remaining */
  if (_monitor_selected == m) {
    _monitor_selected = monitor_list;
  }
}

/*
 * Get monitor by output ID.
 */
Monitor*
monitor_get_by_output(xcb_randr_output_t output)
{
  for (Monitor* m = monitor_list; m != NULL; m = m->next) {
    if (m->output == output)
      return m;
  }
  return NULL;
}

/*
 * Get the currently selected monitor.
 */
Monitor*
monitor_get_selected(void)
{
  return _monitor_selected;
}

/*
 * Set the currently selected monitor.
 */
void
monitor_set_selected(Monitor* m)
{
  _monitor_selected = m;
  if (m != NULL) {
    LOG_DEBUG("Selected monitor: output=%u", m->output);
  }
}

/*
 * Check if a tag is visible on a monitor.
 */
bool
monitor_tag_visible(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS)
    return false;
  return (m->tagset & MONITOR_TAG_MASK(tag)) != 0;
}

/*
 * Set the visible tags on a monitor.
 */
void
monitor_set_tagset(Monitor* m, uint32_t tagset)
{
  if (m == NULL)
    return;
  m->prevtagset = m->tagset;
  m->tagset     = tagset & MONITOR_ALL_TAGS;
  LOG_DEBUG("Monitor tagset changed: output=%u, tagset=%u", m->output, m->tagset);
}

/*
 * Add a tag to the visible set.
 */
void
monitor_tag_add(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS)
    return;
  m->tagset |= MONITOR_TAG_MASK(tag);
}

/*
 * Remove a tag from the visible set.
 */
void
monitor_tag_remove(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS)
    return;
  m->tagset &= ~MONITOR_TAG_MASK(tag);
}

/*
 * Toggle a tag in the visible set.
 */
void
monitor_tag_toggle(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS)
    return;
  m->tagset ^= MONITOR_TAG_MASK(tag);
}

/*
 * Check if a monitor has any clients.
 */
bool
monitor_has_clients(Monitor* m)
{
  if (m == NULL)
    return false;
  return m->client_count > 0;
}

/*
 * Get the number of clients on a monitor.
 */
uint32_t
monitor_client_count(Monitor* m)
{
  if (m == NULL)
    return 0;
  return m->client_count;
}

/*
 * Iterate over all monitors.
 */
void
monitor_foreach(void (*callback)(Monitor* m))
{
  Monitor* current = monitor_list;
  while (current != NULL) {
    Monitor* next = current->next;
    callback(current);
    current = next;
  }
}

/*
 * Set monitor geometry.
 */
void
monitor_set_geometry(Monitor* m, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
  if (m == NULL)
    return;
  m->x      = x;
  m->y      = y;
  m->width  = width;
  m->height = height;
}

/*
 * Get monitor geometry.
 */
void
monitor_get_geometry(const Monitor* m, int16_t* x, int16_t* y, uint16_t* width, uint16_t* height)
{
  if (m == NULL)
    return;
  if (x != NULL)
    *x = m->x;
  if (y != NULL)
    *y = m->y;
  if (width != NULL)
    *width = m->width;
  if (height != NULL)
    *height = m->height;
}
