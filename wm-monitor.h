#ifndef _WM_MONITOR_H_
#define _WM_MONITOR_H_

#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include "wm-hub.h"

/*
 * Forward declaration for Client.
 * Client is defined separately to allow modular compilation.
 */
typedef struct Client Client;

/*
 * Monitor entity for managing displays.
 *
 * A Monitor represents a physical display (RandR output) and holds:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Tag state (visible tags)
 * - Tiling parameters (master factor, master count)
 * - Client list
 * - Bar reference
 */

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
 *
 * Embeds HubTarget for hub registration.
 * Holds all display properties needed for tiling WM.
 */
typedef struct Monitor {
  /* Hub target - must be first for casting */
  HubTarget target;

  /* RandR properties */
  xcb_randr_output_t output;  /* RandR output ID */
  xcb_randr_crtc_t   crtc;     /* RandR CRTC ID (XCB_NONE if disabled) */

  /* Geometry */
  int16_t  x;       /* Screen x position */
  int16_t  y;       /* Screen y position */
  uint16_t width;   /* Resolution width */
  uint16_t height;  /* Resolution height */

  /* Tag state */
  uint32_t tagset;     /* Currently visible tags (bitmask) */
  uint32_t prevtagset; /* Previous tagset for switching */

  /* Tiling parameters */
  float mfact;   /* Master factor (0.0 - 1.0), proportion of screen for master area */
  int   nmaster; /* Number of clients in master area */

  /* Client list */
  Client* clients; /* First client on this monitor */
  Client* sel;     /* Selected (focused) client on this monitor */
  Client* stack;   /* Stacking order (bottom to top) */

  /* Bar */
  void* bar; /* Bar widget reference (type-erased for now) */

  /* List linkage */
  struct Monitor* next; /* Next monitor in global list */

} Monitor;

/*
 * Monitor lifecycle
 */

/*
 * Create a new monitor for the given RandR output.
 * Registers the monitor with the hub.
 *
 * @param output The RandR output ID
 * @return New Monitor, or NULL on allocation failure
 */
Monitor* monitor_create(xcb_randr_output_t output);

/*
 * Destroy a monitor.
 * Unregisters from hub, unmanages all clients.
 *
 * @param m The monitor to destroy
 */
void monitor_destroy(Monitor* m);

/*
 * Monitor list management
 */

/*
 * Initialize the global monitor list.
 * Called once at startup.
 */
void monitor_list_init(void);

/*
 * Destroy all monitors in the global list.
 * Called at shutdown.
 */
void monitor_list_shutdown(void);

/*
 * Get the first monitor in the global list.
 *
 * @return First monitor, or NULL if no monitors
 */
Monitor* monitor_list_get_first(void);

/*
 * Get the next monitor after the given one.
 *
 * @param m Current monitor
 * @return Next monitor, or NULL if m was the last
 */
Monitor* monitor_list_get_next(Monitor* m);

/*
 * Add a monitor to the global list.
 * Called during RandR setup.
 *
 * @param m Monitor to add
 */
void monitor_list_add(Monitor* m);

/*
 * Remove a monitor from the global list.
 *
 * @param m Monitor to remove
 */
void monitor_list_remove(Monitor* m);

/*
 * Find a monitor by its RandR output ID.
 *
 * @param output RandR output ID
 * @return Monitor with matching output, or NULL if not found
 */
Monitor* monitor_by_output(xcb_randr_output_t output);

/*
 * Get the currently selected monitor.
 * Used for TARGET_CURRENT_MONITOR resolution.
 *
 * @return The selected monitor, or first monitor if none selected
 */
Monitor* monitor_get_selected(void);

/*
 * Set the currently selected monitor.
 *
 * @param m The monitor to select, or NULL for none
 */
void monitor_set_selected(Monitor* m);

/*
 * Utility functions
 */

/*
 * Check if a monitor has any clients.
 *
 * @param m Monitor to check
 * @return true if monitor has clients, false otherwise
 */
bool monitor_has_clients(Monitor* m);

/*
 * Get the number of clients on a monitor.
 *
 * @param m Monitor to query
 * @return Number of clients
 */
uint32_t monitor_client_count(Monitor* m);

/*
 * Check if a tag is visible on a monitor.
 *
 * @param m Monitor to check
 * @param tag Tag index (0-8)
 * @return true if tag is in tagset
 */
bool monitor_tag_visible(Monitor* m, int tag);

/*
 * Set the visible tags on a monitor.
 *
 * @param m Monitor to modify
 * @param tagset New tagset (bitmask)
 */
void monitor_set_tagset(Monitor* m, uint32_t tagset);

/*
 * Add a tag to the visible set.
 *
 * @param m Monitor to modify
 * @param tag Tag index (0-8)
 */
void monitor_tag_add(Monitor* m, int tag);

/*
 * Remove a tag from the visible set.
 *
 * @param m Monitor to modify
 * @param tag Tag index (0-8)
 */
void monitor_tag_remove(Monitor* m, int tag);

/*
 * Toggle a tag in the visible set.
 *
 * @param m Monitor to modify
 * @param tag Tag index (0-8)
 */
void monitor_tag_toggle(Monitor* m, int tag);

/*
 * Iterate over all monitors.
 *
 * @param callback Function called for each monitor
 */
void monitor_foreach(void (*callback)(Monitor* m));

#endif /* _WM_MONITOR_H_ */
