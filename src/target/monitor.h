/*
 * Monitor Target - Physical display entity for the window manager
 *
 * A Monitor represents a physical display (RandR output). It owns:
 * - RandR properties (output, crtc)
 * - Geometry (position and resolution)
 * - Adopted state machines
 * - Tag state and layout configuration
 * - Client list associations
 *
 * This is a forward declaration header. Full implementation comes later.
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
 * Monitor structure
 * Represents a physical display managed by the window manager.
 *
 * Note: This is a stub implementation. Full functionality will be added
 * when monitor management is implemented.
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

  /* Client associations */
  struct Client* clients; /* first client on this monitor */
  struct Client* sel;     /* selected/focused client */
  struct Client* stack;   /* stacking order (bottom to top) */

  /* Bar (optional) */
  void* bar;

  /* Next monitor in the list */
  struct Monitor* next;

} Monitor;

/*
 * Create a new Monitor for a RandR output.
 */
Monitor* monitor_create(xcb_randr_output_t output);

/*
 * Destroy a Monitor.
 */
void monitor_destroy(Monitor* m);

/*
 * Get monitor by output ID.
 */
Monitor* monitor_get_by_output(xcb_randr_output_t output);

/*
 * Set monitor geometry.
 */
void monitor_set_geometry(Monitor* m, int16_t x, int16_t y, uint16_t width, uint16_t height);

/*
 * Get monitor geometry.
 */
void monitor_get_geometry(const Monitor* m, int16_t* x, int16_t* y, uint16_t* width, uint16_t* height);

#endif /* _MONITOR_H_ */
