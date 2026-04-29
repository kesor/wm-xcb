#include <stdlib.h>
#include <string.h>

#include "wm-log.h"
#include "wm-monitor.h"
#include "wm-window-list.h"

/*
 * Monitor implementation
 *
 * Manages display entities registered with the hub.
 */

/*
 * Client forward declaration.
 * The actual Client struct is defined separately.
 * Monitor holds pointers to clients for client list management.
 */

/*
 * Global monitor list
 */
static Monitor* monitor_list     = NULL;
static Monitor* selected_monitor = NULL;
static uint32_t monitor_count    = 0;

/*
 * Default monitor values
 */
#define DEFAULT_MFACT   0.5f
#define DEFAULT_NMASTER 1
#define DEFAULT_TAGSET  MONITOR_TAG_MASK(0)

Monitor*
monitor_create(xcb_randr_output_t output)
{
  Monitor* m = malloc(sizeof(Monitor));
  if (m == NULL) {
    LOG_ERROR("Failed to allocate monitor");
    return NULL;
  }

  /* Initialize HubTarget */
  m->target.id         = (TargetID) output;
  m->target.type       = TARGET_TYPE_MONITOR;
  m->target.registered = false;

  /* RandR properties */
  m->output = output;
  m->crtc   = XCB_NONE;

  /* Geometry - default to something reasonable, will be updated by RandR */
  m->x      = 0;
  m->y      = 0;
  m->width  = 0;
  m->height = 0;

  /* Tag state */
  m->tagset     = DEFAULT_TAGSET;
  m->prevtagset = DEFAULT_TAGSET;

  /* Tiling parameters */
  m->mfact   = DEFAULT_MFACT;
  m->nmaster = DEFAULT_NMASTER;

  /* Client list - will be populated by client management */
  m->clients = NULL;
  m->sel     = NULL;
  m->stack   = NULL;

  /* Bar - type-erased, set by bar component */
  m->bar = NULL;

  /* List linkage */
  m->next = NULL;

  /* Register with hub */
  hub_register_target(&m->target);

  /* Add to global monitor list */
  monitor_list_add(m);

  LOG_DEBUG("Created monitor: output=%u", output);
  return m;
}

void
monitor_destroy(Monitor* m)
{
  if (m == NULL) {
    return;
  }

  LOG_DEBUG("Destroying monitor: output=%u", m->output);

  /* Remove from global list if present */
  monitor_list_remove(m);

  /* Unregister from hub only if registration succeeded */
  if (m->target.registered) {
    hub_unregister_target(m->target.id);
  }

  /* Note: Clients are not destroyed here.
   * They should be unmanaged first by the caller.
   * This follows the pattern where a monitor dying doesn't kill clients,
   * they just become unmanaged and can be reassigned. */

  /* Clear selected if this was selected */
  if (selected_monitor == m) {
    selected_monitor = monitor_list_get_first();
  }

  /* Free the monitor */
  free(m);
  LOG_DEBUG("Monitor destroyed");
}

/*
 * Monitor list management
 */

void
monitor_list_init(void)
{
  LOG_DEBUG("Initializing monitor list");
  monitor_list     = NULL;
  selected_monitor = NULL;
  monitor_count    = 0;
}

void
monitor_list_shutdown(void)
{
  LOG_DEBUG("Shutting down monitor list (count=%u)", monitor_count);

  /* Destroy all monitors through the normal removal path.
   * This keeps list bookkeeping (monitor_count, selected_monitor) correct. */
  while (monitor_list != NULL) {
    monitor_destroy(monitor_list);
  }

  monitor_list     = NULL;
  selected_monitor = NULL;
  monitor_count    = 0;

  LOG_DEBUG("Monitor list shutdown complete");
}

Monitor*
monitor_list_get_first(void)
{
  return monitor_list;
}

Monitor*
monitor_list_get_next(Monitor* m)
{
  if (m == NULL) {
    return NULL;
  }
  return m->next;
}

void
monitor_list_add(Monitor* m)
{
  if (m == NULL) {
    return;
  }

  /* Add to head of list */
  m->next      = monitor_list;
  monitor_list = m;
  monitor_count++;

  /* Always select the most recently added monitor */
  selected_monitor = m;

  LOG_DEBUG("Added monitor to list: output=%u, count=%u", m->output, monitor_count);
}

void
monitor_list_remove(Monitor* m)
{
  if (m == NULL) {
    return;
  }

  /* Remove from linked list */
  if (monitor_list == m) {
    /* First element */
    monitor_list = m->next;
  } else {
    /* Find previous element */
    Monitor* prev = monitor_list;
    while (prev != NULL && prev->next != m) {
      prev = prev->next;
    }
    if (prev != NULL) {
      prev->next = m->next;
    }
  }

  m->next = NULL;
  monitor_count--;

  /* If removed selected monitor, select first remaining */
  if (selected_monitor == m) {
    selected_monitor = monitor_list;
  }

  LOG_DEBUG("Removed monitor from list: output=%u, count=%u", m->output, monitor_count);
}

Monitor*
monitor_by_output(xcb_randr_output_t output)
{
  Monitor* current = monitor_list;
  while (current != NULL) {
    if (current->output == output) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

Monitor*
monitor_get_selected(void)
{
  return selected_monitor;
}

void
monitor_set_selected(Monitor* m)
{
  selected_monitor = m;
  if (m != NULL) {
    LOG_DEBUG("Selected monitor: output=%u", m->output);
  }
}

/*
 * Utility functions
 */

bool
monitor_has_clients(Monitor* m)
{
  if (m == NULL) {
    return false;
  }
  return m->clients != NULL;
}

uint32_t
monitor_client_count(Monitor* m)
{
  if (m == NULL) {
    return 0;
  }

  /* Iterate using Monitor's per-monitor client list linkage */
  uint32_t count = 0;
  Client*  c     = (Client*) m->clients;
  while (c != NULL) {
    count++;
    c = c->next;
  }
  return count;
}

bool
monitor_tag_visible(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS) {
    return false;
  }
  return (m->tagset & MONITOR_TAG_MASK(tag)) != 0;
}

void
monitor_set_tagset(Monitor* m, uint32_t tagset)
{
  if (m == NULL) {
    return;
  }
  m->prevtagset = m->tagset;
  m->tagset     = tagset & MONITOR_ALL_TAGS;
  LOG_DEBUG("Monitor tagset changed: output=%u, tagset=%u", m->output, m->tagset);
}

void
monitor_tag_add(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS) {
    return;
  }
  m->tagset |= MONITOR_TAG_MASK(tag);
}

void
monitor_tag_remove(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS) {
    return;
  }
  m->tagset &= ~MONITOR_TAG_MASK(tag);
}

void
monitor_tag_toggle(Monitor* m, int tag)
{
  if (m == NULL || tag < 0 || tag >= MONITOR_NUM_TAGS) {
    return;
  }
  m->tagset ^= MONITOR_TAG_MASK(tag);
}

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
