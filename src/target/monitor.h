/*
 * Monitor Target - Physical display entity for the window manager
 *
 * A Monitor represents a physical display (RandR output). This header
 * provides the Monitor type definition and its public API declarations.
 *
 * Core responsibilities:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Target registration with the Hub
 * - State machine storage (for components to attach their data)
 *
 * The tags this monitor views are not stored here. The tag-manager component
 * keeps that mask in the monitor's tag-view state machine, so the machine and
 * the monitor cannot disagree about which tags are visible.
 *
 * Features like tiling and bar display are implemented as separate components
 * that query monitors via the Hub. A client keeps a pointer to its monitor, so
 * monitor_destroy() moves the clients it held onto the monitor a new client
 * would get, or onto none when it was the last.
 */

#ifndef _MONITOR_H_
#define _MONITOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>

#include "src/core/wm-hub.h"
#include "src/sm/sm.h"

/*
 * Monitor structure
 * Represents a physical display managed by the window manager.
 *
 * Core only - layout, bar, and client tracking are separate components.
 * State machines are allocated on demand by components.
 */
typedef struct Monitor {
  /* Base target for Hub registration */
  HubTarget target;

  /* RandR properties */
  xcb_randr_output_t output;
  xcb_randr_crtc_t   crtc;

  /* Geometry */
  int16_t  x;
  int16_t  y;
  uint16_t width;
  uint16_t height;

  /* Adopted state machines - dynamically allocated on demand by components.
   * Components store their data here, keyed by name (e.g., "pertag"). */
  struct {
    StateMachine** machines; /* array of SM pointers */
    char**         names;    /* corresponding names */
    uint32_t       count;    /* number of entries */
    uint32_t       capacity; /* allocated capacity */
  } sms;

  /* Next monitor in the linked list */
  struct Monitor* next;

} Monitor;

/*
 * Monitor lifecycle
 */

/*
 * Create a new Monitor for a RandR output.
 */
Monitor* monitor_create(xcb_randr_output_t output);

/*
 * Destroy a Monitor.
 */
void monitor_destroy(Monitor* m);

/*
 * Monitor list management
 */

/*
 * Initialize the monitor list.
 * Clears any stale monitors from previous runs.
 */
void monitor_list_init(void);

/*
 * Shutdown the monitor list.
 * Destroys all monitors in the list and cleans up resources.
 */
void monitor_list_shutdown(void);

/*
 * Get the first monitor in the global list.
 */
Monitor* monitor_list_get_first(void);

/*
 * Get the next monitor after the given one.
 */
Monitor* monitor_list_get_next(Monitor* m);

/*
 * Add a monitor to the global list.
 */
void monitor_list_add(Monitor* m);

/*
 * Remove a monitor from the global list.
 */
void monitor_list_remove(Monitor* m);

/*
 * Get monitor by output ID.
 */
Monitor* monitor_get_by_output(xcb_randr_output_t output);

/*
 * Alias for monitor_get_by_output for backwards compatibility.
 */
#define monitor_by_output(output) monitor_get_by_output(output)

/*
 * Get the currently selected monitor.
 */
Monitor* monitor_get_selected(void);

/*
 * Set the currently selected monitor.
 */
void monitor_set_selected(Monitor* m);

/*
 * Get the TargetID of the currently selected monitor.
 * Returns TARGET_ID_NONE if no monitor is selected.
 * Used by keybinding component to resolve TARGET_CURRENT_MONITOR.
 */
TargetID monitor_get_current_monitor(void);

/*
 * Selection - selected monitor is always the most recently added one
 * (this is a simplification - can be updated later)
 */
extern Monitor* selected_monitor;

/*
 * Iterate over all monitors.
 */
void monitor_foreach(void (*callback)(Monitor* m));

/*
 * Set monitor geometry.
 */
void monitor_set_geometry(Monitor* m, int16_t x, int16_t y, uint16_t width, uint16_t height);

/*
 * Get monitor geometry.
 */
void monitor_get_geometry(const Monitor* m, int16_t* x, int16_t* y, uint16_t* width, uint16_t* height);

/*
 * State Machine / Component Data Management
 *
 * Components attach their data to monitors using this storage.
 * Data is stored by name - components retrieve it later via monitor_get_sm().
 *
 * Example: pertag stores Pertag* as monitor_set_sm(m, "pertag", (SM*)pt)
 *
 * The machine stored under "pertag" is a plain allocation, not a
 * StateMachine, so the monitor free()s that slot where it sm_destroy()s any
 * other.
 */

/*
 * Get component data attached to this monitor by name.
 *
 * Returns the data pointer stored via monitor_set_sm(), or
 * NULL if no data has been attached for the given name.
 */
StateMachine* monitor_get_sm(Monitor* m, const char* sm_name);

/*
 * Set component data for this monitor.
 * Used by components to attach their data (e.g., Pertag*, layout state, etc.).
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
bool monitor_set_sm(Monitor* m, const char* sm_name, StateMachine* sm);

#endif /* _MONITOR_H_ */
