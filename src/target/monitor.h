/*
 * Monitor Target - Physical display entity for the window manager
 *
 * A Monitor represents a physical display (RandR output). This header
 * provides the Monitor type definition and its public API declarations.
 *
 * Core responsibilities:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Tag state (which tags this monitor is viewing)
 * - Target registration with the Hub
 * - State machine storage (for components to attach their data)
 *
 * Note: Tag state is stored here because it's specific to monitor-view
 * semantics (a monitor "views" certain tags). The actual tag management
 * is handled by the tag-manager component.
 *
 * Features like tiling, bar display, and client associations are implemented
 * as separate components that query monitors via the Hub.
 */

#ifndef _MONITOR_H_
#define _MONITOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>

#include "../sm/sm.h"
#include "wm-hub.h"

/*
 * Number of available tags (bits in tag mask)
 * Standard dwm uses 9 tags (0-8)
 */
#define MONITOR_NUM_TAGS    9

/*
 * Tag mask helpers
 */
#define MONITOR_TAG_MASK(n) ((uint32_t) 1 << ((n) % MONITOR_NUM_TAGS))
#define MONITOR_ALL_TAGS    ((uint32_t) ((1 << MONITOR_NUM_TAGS) - 1))

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

  /* Tag state - which tags this monitor is viewing */
  uint32_t tagset;     /* currently visible tags */
  uint32_t prevtagset; /* previous tagset for switching */

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
 * Tag operations
 */

/*
 * Check if a tag is visible on a monitor.
 */
bool monitor_tag_visible(Monitor* m, int tag);

/*
 * Set the visible tags on a monitor.
 */
void monitor_set_tagset(Monitor* m, uint32_t tagset);

/*
 * Add a tag to the visible set.
 */
void monitor_tag_add(Monitor* m, int tag);

/*
 * Remove a tag from the visible set.
 */
void monitor_tag_remove(Monitor* m, int tag);

/*
 * Toggle a tag in the visible set.
 */
void monitor_tag_toggle(Monitor* m, int tag);

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
 *
 * If sm is NULL, removes any existing entry for that name.
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should free the data to avoid leaks.
 */
bool monitor_set_sm(Monitor* m, const char* sm_name, StateMachine* sm);

#endif /* _MONITOR_H_ */
