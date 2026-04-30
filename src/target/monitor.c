/*
 * Monitor Target Implementation
 *
 * A Monitor represents a physical display (RandR output).
 *
 * Core responsibilities:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Tag state (which tags this monitor is viewing)
 * - Hub registration
 * - State machine storage for components
 *
 * Features like tiling, bar display, and client associations are
 * implemented as separate components that query monitors via the Hub.
 */

#include <stdlib.h>
#include <string.h>

#include "monitor.h"
#include "wm-hub.h"
#include "wm-log.h"

/*
 * Monitor list
 */
static Monitor* monitor_list     = NULL;
Monitor*        selected_monitor = NULL;

/*
 * Monitor initialization helper
 */
static void
monitor_properties_init(Monitor* m, xcb_randr_output_t output)
{
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

  /* Initialize SM storage */
  m->sms.machines = NULL;
  m->sms.names    = NULL;
  m->sms.count    = 0;
  m->sms.capacity = 0;
}

/*
 * Helper: find SM index by name.
 * Returns -1 if not found.
 */
static int32_t
monitor_sm_find(const Monitor* m, const char* sm_name)
{
  if (m == NULL || sm_name == NULL)
    return -1;

  for (uint32_t i = 0; i < m->sms.count; i++) {
    if (m->sms.names[i] != NULL && strcmp(m->sms.names[i], sm_name) == 0) {
      return (int32_t) i;
    }
  }
  return -1;
}

/*
 * Helper: add or update SM for a name.
 * Takes ownership of `sm` - any previously registered SM for this name
 * will be destroyed (unless it's the same pointer to avoid self-destruction).
 * The sm_name string is copied internally.
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should handle cleanup of the passed SM.
 */
static bool
monitor_sm_set_internal(Monitor* m, const char* sm_name, StateMachine* sm)
{
  int32_t idx = monitor_sm_find(m, sm_name);

  if (idx >= 0) {
    /* Update existing - destroy old SM if different from new one */
    if (m->sms.machines[idx] != NULL && m->sms.machines[idx] != sm) {
      sm_destroy(m->sms.machines[idx]);
    }
    m->sms.machines[idx] = sm;
    /* Clear name if setting to NULL (data cleanup case) */
    if (sm == NULL && m->sms.names[idx] != NULL) {
      free(m->sms.names[idx]);
      m->sms.names[idx] = NULL;
    }
    return true;
  }

  /* Add new SM */
  if (m->sms.count >= m->sms.capacity) {
    uint32_t       new_capacity = m->sms.capacity == 0 ? 4 : m->sms.capacity * 2;
    StateMachine** new_machines = NULL;
    char**         new_names    = NULL;

    /* Reallocate machines first */
    new_machines = realloc(m->sms.machines, new_capacity * sizeof(StateMachine*));
    if (new_machines == NULL) {
      LOG_ERROR("Failed to expand SM machines storage for monitor");
      return false;
    }

    /* Reallocate names */
    new_names = realloc(m->sms.names, new_capacity * sizeof(char*));
    if (new_names == NULL) {
      free(new_machines);
      LOG_ERROR("Failed to expand SM names storage for monitor");
      return false;
    }

    /* Both allocations succeeded, update atomically */
    m->sms.machines = new_machines;
    m->sms.names    = new_names;
    m->sms.capacity = new_capacity;
  }

  /* strdup the name - check for failure */
  char* copied_name = sm_name ? strdup(sm_name) : NULL;
  if (sm_name != NULL && copied_name == NULL) {
    LOG_ERROR("Failed to copy SM name for monitor");
    /* Don't add the SM if we can't store the name */
    return false;
  }

  m->sms.machines[m->sms.count] = sm;
  m->sms.names[m->sms.count]    = copied_name;
  m->sms.count++;

  return true;
}

/*
 * Helper: free all SM storage (names and machines array)
 * Handles component data cleanup for non-SM data (like Pertag).
 */
static void
monitor_sm_storage_free(Monitor* m)
{
  for (uint32_t i = 0; i < m->sms.count; i++) {
    if (m->sms.names[i] != NULL) {
      /* Check for component data that needs freeing */
      /* Pertag stores Pertag* as void* in machines[i] - free it */
      if (strcmp(m->sms.names[i], "pertag") == 0 && m->sms.machines[i] != NULL) {
        free(m->sms.machines[i]);
      }
      free(m->sms.names[i]);
    }
  }
  free(m->sms.machines);
  free(m->sms.names);
  m->sms.machines = NULL;
  m->sms.names    = NULL;
  m->sms.count    = 0;
  m->sms.capacity = 0;
}

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
  monitor_list     = NULL;
  selected_monitor = NULL;
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
  monitor_list     = NULL;
  selected_monitor = NULL;
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

  /* Initialize properties */
  monitor_properties_init(m, output);

  /* Add to list */
  m->next      = monitor_list;
  monitor_list = m;

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
    /* Free SM storage */
    monitor_sm_storage_free(m);
    free(m);
    return NULL;
  }

  /* Only select after successful registration */
  selected_monitor = m;

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

  /* If this was the selected monitor, select the first remaining */
  if (selected_monitor == m) {
    selected_monitor = monitor_list_get_first();
  }

  /* Free SM storage */
  monitor_sm_storage_free(m);

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
  selected_monitor = m;
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
  if (selected_monitor == m) {
    selected_monitor = monitor_list;
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
  return selected_monitor;
}

/*
 * Get the TargetID of the currently selected monitor.
 * Returns TARGET_ID_NONE if no monitor is selected.
 */
TargetID
monitor_get_current_monitor(void)
{
  if (selected_monitor == NULL) {
    return TARGET_ID_NONE;
  }
  return selected_monitor->target.id;
}

/*
 * Set the currently selected monitor.
 */
void
monitor_set_selected(Monitor* m)
{
  selected_monitor = m;
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

/*
 * Get state machine by name.
 * Returns NULL if no SM is registered for this name.
 */
StateMachine*
monitor_get_sm(Monitor* m, const char* sm_name)
{
  if (m == NULL || sm_name == NULL)
    return NULL;

  int32_t idx = monitor_sm_find(m, sm_name);
  if (idx < 0)
    return NULL;

  return m->sms.machines[idx];
}

/*
 * Set a state machine for this monitor.
 * The SM is stored by name - components can retrieve it later via monitor_get_sm().
 *
 * If sm is NULL, removes any existing entry for that name.
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should destroy the SM to avoid leaks.
 */
bool
monitor_set_sm(Monitor* m, const char* sm_name, StateMachine* sm)
{
  if (m == NULL || sm_name == NULL)
    return false;

  return monitor_sm_set_internal(m, sm_name, sm);
}
