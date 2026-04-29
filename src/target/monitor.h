/*
 * Monitor Target - Physical display entity for the window manager
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

#ifndef _MONITOR_H_
#define _MONITOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>

#include "../../wm-hub.h"
#include "../sm/sm.h"

/*
 * Number of available tags (bits in tag mask)
 * Standard dwm uses 9 tags (0-8)
 */
#define MONITOR_NUM_TAGS 9

/*
 * Tag mask helpers
 */
#define MONITOR_TAG_MASK(n) ((uint32_t)1 << ((n) % MONITOR_NUM_TAGS))
#define MONITOR_ALL_TAGS   ((uint32_t)((1 << MONITOR_NUM_TAGS) - 1))

/*
 * Monitor structure
 * Represents a physical display managed by the window manager.
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

  /* Tag state */
  uint32_t tagset;      /* currently visible tags */
  uint32_t prevtagset;  /* previous tagset for switching */

  /* Layout configuration */
  float mfact;  /* master factor (0.0 - 1.0) */
  int   nmaster; /* number of clients in master area */

  /* Client tracking - decoupled design (use hub to query) */
  uint32_t client_count;
  xcb_window_t sel_window; /* window ID of selected client */
  xcb_window_t stack_head; /* window ID of bottom of stack */

  /* Bar (optional) */
  void* bar;

  /* Next monitor in the list */
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
 * Initialize the monitor list (no-op, list is statically initialized).
 */
void monitor_list_init(void);

/*
 * Shutdown the monitor list (no-op, list is cleaned up on program exit).
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
 * Selection - selected monitor is always the most recently added one
 * (this is a simplification - can be updated later)
 */
extern Monitor* _monitor_selected;

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
 * Utility functions
 */

/*
 * Check if a monitor has any clients.
 * Note: This checks the counter - actual client query goes through hub.
 */
bool monitor_has_clients(Monitor* m);

/*
 * Get the number of clients on a monitor.
 */
uint32_t monitor_client_count(Monitor* m);

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

#endif /* _MONITOR_H_ */
