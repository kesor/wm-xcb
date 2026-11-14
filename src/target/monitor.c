/*
 * Monitor Target Implementation
 *
 * A Monitor represents a physical display (RandR output).
 *
 * Core responsibilities:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Hub registration
 * - State machine storage for components
 *
 * Features like tiling and bar display are implemented as separate
 * components that query monitors via the Hub. A client keeps a pointer to the
 * monitor it is on, so destroying one moves the clients it held onto the
 * monitor a new client would get, or onto none when it was the last.
 */

#include <stdlib.h>
#include <string.h>

#include "src/core/wm-hub.h"
#include "src/core/wm-log.h"
#include "src/target/client.h"
#include "src/target/monitor.h"

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
 * Free whatever occupies the machine slot at `idx` and empty the slot.
 *
 * The "pertag" slot holds a Pertag rather than a StateMachine: pertag keeps
 * its data where a machine goes, so that slot is freed instead of destroyed.
 */
static void
monitor_sm_free_machine(Monitor* m, uint32_t idx)
{
  if (m->sms.machines[idx] == NULL)
    return;

  if (m->sms.names[idx] != NULL && strcmp(m->sms.names[idx], "pertag") == 0) {
    free(m->sms.machines[idx]);
  } else {
    sm_destroy(m->sms.machines[idx]);
  }
  m->sms.machines[idx] = NULL;
}

/*
 * Helper: add or update SM for a name.
 * Takes ownership of `sm` - any previously registered SM for this name
 * will be destroyed (unless it's the same pointer to avoid self-destruction).
 * The sm_name string is copied internally.
 *
 * If sm is NULL, removes any existing entry for that name (destroying it).
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should handle cleanup of the passed SM.
 */
static bool
monitor_sm_set_internal(Monitor* m, const char* sm_name, StateMachine* sm)
{
  int32_t idx = monitor_sm_find(m, sm_name);

  if (idx >= 0) {
    /* A NULL machine removes the entry, so the next set of this name starts
     * from an empty table instead of appending a second entry for it. */
    if (sm == NULL) {
      uint32_t slot = (uint32_t) idx;
      uint32_t last = m->sms.count - 1;

      monitor_sm_free_machine(m, slot);
      free(m->sms.names[slot]);
      m->sms.names[slot] = NULL;

      if (slot != last) {
        m->sms.machines[slot] = m->sms.machines[last];
        m->sms.names[slot]    = m->sms.names[last];
      }
      m->sms.machines[last] = NULL;
      m->sms.names[last]    = NULL;
      m->sms.count--;
      return true;
    }

    /* Update existing - destroy old SM if different from new one */
    if (m->sms.machines[idx] != sm) {
      monitor_sm_free_machine(m, (uint32_t) idx);
    }
    m->sms.machines[idx] = sm;
    return true;
  }

  /* Removing a name that was never set is not an error. */
  if (sm == NULL)
    return true;

  /* Add new SM */
  if (m->sms.count >= m->sms.capacity) {
    uint32_t new_capacity = m->sms.capacity == 0 ? 4 : m->sms.capacity * 2;

    /* Use temp pointers to avoid corrupting state on failure */
    StateMachine** tmp_machines = realloc(m->sms.machines, new_capacity * sizeof(StateMachine*));
    if (tmp_machines == NULL) {
      LOG_ERROR("Failed to expand SM machines storage for monitor");
      return false;
    }
    m->sms.machines = tmp_machines;

    char** tmp_names = realloc(m->sms.names, new_capacity * sizeof(char*));
    if (tmp_names == NULL) {
      LOG_ERROR("Failed to expand SM names storage for monitor");
      return false;
    }
    m->sms.names    = tmp_names;
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
  /*
   * Every stored machine belongs to this monitor: a component created it during
   * adoption and nobody else holds it, so destroying the monitor destroys them.
   * The one exception is "pertag", whose slot holds a Pertag* rather than a
   * StateMachine*.
   */
  for (uint32_t i = 0; i < m->sms.count; i++) {
    monitor_sm_free_machine(m, i);
    free(m->sms.names[i]);
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
      monitor_sm_storage_free(monitor_list);
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
  /*
   * Through monitor_destroy, not free(): a monitor owns the state machines
   * stored on it and its name table, and free() here dropped all of them.
   */
  while (monitor_list != NULL) {
    monitor_destroy(monitor_list);
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
  m->target.type_id    = hub_get_target_type_id_by_name("monitor");
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

  /*
   * Before the free and after the list lost this monitor: a client holds a
   * Monitor* for its whole life, and nothing else clears it, so every client
   * this monitor held would read a freed target id in its next tile request.
   */
  client_rehome_from_monitor(m);

  /*
   * Unregister first: the components' unadopt hooks run here, and they reach
   * the machines this monitor stores - the tag view machine's data is freed
   * through it. Freeing the storage first left those hooks writing into a
   * cleared table.
   */
  if (m->target.registered) {
    hub_unregister_target(m->target.id);
  }

  /* Free SM storage */
  monitor_sm_storage_free(m);

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
 * The name is copied; the monitor keeps the copy.
 *
 * A successful call takes ownership of `sm`: the monitor destroys it when
 * another machine is set under the same name, and when the monitor itself is
 * destroyed.
 *
 * Passing NULL destroys the machine stored under `sm_name` and removes its
 * entry, so monitor_get_sm() returns NULL for that name afterwards. Removing a
 * name that holds nothing is not an error.
 *
 * Returns true on success, false when the monitor or the name is NULL, when
 * the table could not grow, or when the name could not be copied. A failed
 * call stores nothing and leaves the machine the caller passed still the
 * caller's, to destroy or free the way a successful call would have.
 */
bool
monitor_set_sm(Monitor* m, const char* sm_name, StateMachine* sm)
{
  if (m == NULL || sm_name == NULL)
    return false;

  return monitor_sm_set_internal(m, sm_name, sm);
}
