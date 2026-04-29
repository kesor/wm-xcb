#ifndef _WM_MONITOR_H_
#define _WM_MONITOR_H_

#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include "wm-hub.h"

/*
 * Monitor Target
 *
 * Represents a display/output. Each Monitor is a HubTarget that can:
 * - Register with the Hub as a TARGET_TYPE_MONITOR
 * - Adopt compatible components (tiling, bar, etc.)
 * - Own state machines for its states
 */

/* Forward declarations */
typedef struct Monitor Monitor;
typedef struct MonitorComponent MonitorComponent;
typedef struct Client Client;

/* Monitor state machine states */
enum {
  MONITOR_STATE_ACTIVE   = 0,  /* Monitor is active and visible */
  MONITOR_STATE_INACTIVE = 1,  /* Monitor is disconnected or inactive */
  MONITOR_STATE_OFFLINE = 2,  /* Monitor is offline */
};

/* Monitor structure - represents a display/output */
struct Monitor {
  /* Hub target integration */
  HubTarget base;  /* id = output, type = TARGET_TYPE_MONITOR */

  /* RandR properties */
  xcb_randr_output_t output;
  xcb_randr_crtc_t   crtc;

  /* Geometry */
  int16_t    x, y;
  uint16_t   width, height;
  uint16_t   mwidth, mheight;  /* Physical dimensions in mm */

  /* Tiling parameters */
  float      mfact;   /* Master factor (0.0 - 1.0) */
  int        nmaster; /* Number of master windows */

  /* State */
  uint32_t   tagset;      /* Current tag bitmask */
  uint32_t   state;       /* Current state machine state */
  uint32_t   layout;      /* Current layout index */

  /* Clients on this monitor */
  Client*    clients;     /* Head of client list for this monitor */
  Client*    sel;         /* Selected/focused client on this monitor */
  Client*    stack;       /* Client stacking order (top to bottom) */

  /* Hierarchy */
  Monitor*   next;        /* Next monitor in list */
  Monitor*   prev;        /* Previous monitor in list */

  /* Adoption */
  MonitorComponent* components;
};

/* Monitor component - attached to a monitor */
struct MonitorComponent {
  const char*         name;
  void*               data;      /* Component-specific data */
  MonitorComponent*  next;
};

/*
 * Monitor lifecycle
 */

/* Create a new monitor for an output */
Monitor* monitor_create(xcb_randr_output_t output);

/* Destroy a monitor */
void monitor_destroy(Monitor* monitor);

/* Register monitor with hub */
void monitor_register(Monitor* monitor);

/* Unregister monitor from hub */
void monitor_unregister(Monitor* monitor);

/*
 * Monitor modification
 */

/* Set monitor geometry */
void monitor_set_geometry(Monitor* monitor, int16_t x, int16_t y, uint16_t width, uint16_t height);

/* Set physical size */
void monitor_set_physical(Monitor* monitor, uint16_t width_mm, uint16_t height_mm);

/* Set RandR properties */
void monitor_set_randr(Monitor* monitor, xcb_randr_output_t output, xcb_randr_crtc_t crtc);

/* Set master factor */
void monitor_set_mfact(Monitor* monitor, float mfact);

/* Set number of master windows */
void monitor_set_nmaster(Monitor* monitor, int nmaster);

/* Set tagset */
void monitor_set_tagset(Monitor* monitor, uint32_t tagset);

/* Add a tag to tagset */
void monitor_add_tag(Monitor* monitor, uint32_t tag);

/* Remove a tag from tagset */
void monitor_remove_tag(Monitor* monitor, uint32_t tag);

/* Check if tagset has a tag */
bool monitor_has_tag(Monitor* monitor, uint32_t tag);

/* Set layout */
void monitor_set_layout(Monitor* monitor, uint32_t layout);

/* Set state */
void monitor_set_state(Monitor* monitor, uint32_t state);

/*
 * Component adoption
 */

/* Monitor adopts a component */
void monitor_adopt(Monitor* monitor, MonitorComponent* component);

/* Monitor drops a component */
void monitor_drop(Monitor* monitor, const char* component_name);

/* Get adopted component by name */
MonitorComponent* monitor_get_component(Monitor* monitor, const char* name);

/*
 * Client management on monitor
 */

/* Attach client to monitor */
void monitor_attach_client(Monitor* monitor, Client* client);

/* Detach client from monitor */
void monitor_detach_client(Monitor* monitor, Client* client);

/* Focus client on this monitor */
void monitor_focus_client(Monitor* monitor, Client* client);

/*
 * Query
 */

/* Get monitor by output ID */
Monitor* monitor_get_by_output(xcb_randr_output_t output);

/* Get monitor by crtc ID */
Monitor* monitor_get_by_crtc(xcb_randr_crtc_t crtc);

/* Get monitor at position */
Monitor* monitor_at(int16_t x, int16_t y);

/* Get next monitor in list */
Monitor* monitor_next(Monitor* monitor);

/* Get previous monitor in list */
Monitor* monitor_prev(Monitor* monitor);

/* Get monitor list head */
Monitor* monitor_list_head(void);

/* Get monitor count */
uint32_t monitor_count(void);

/* Get selected monitor (currently focused) */
Monitor* monitor_selected(void);

/* Set selected monitor */
void monitor_select(Monitor* monitor);

/*
 * Layout types
 */
enum {
  LAYOUT_TILE     = 0,  /* Tile layout */
  LAYOUT_MONOCLE  = 1,  /* Monocle layout */
  LAYOUT_FLOATING = 2,  /* Floating layout */
  LAYOUT_COUNT    = 3,
};

/*
 * Request types for monitor operations
 */
enum MonitorRequestType {
  REQ_MONITOR_TAG_VIEW    = 300,  /* View specific tag */
  REQ_MONITOR_TAG_TOGGLE  = 301,  /* Toggle tag visibility */
  REQ_MONITOR_TAG_SHIFT   = 302,  /* Move focused client to tag */
  REQ_MONITOR_SET_LAYOUT  = 303,  /* Set layout */
  REQ_MONITOR_NEXT_LAYOUT = 304, /* Cycle to next layout */
  REQ_MONITOR_SET_MFACT   = 305,  /* Set master factor */
  REQ_MONITOR_FOCUS       = 306,  /* Focus monitor */
};

/*
 * Event types emitted by monitors
 */
enum MonitorEventType {
  EVT_MONITOR_CREATED      = 400,  /* Monitor was created */
  EVT_MONITOR_DESTROYED    = 401,  /* Monitor was destroyed */
  EVT_MONITOR_CONNECTED    = 402,  /* Monitor was connected */
  EVT_MONITOR_DISCONNECTED = 403,  /* Monitor was disconnected */
  EVT_MONITOR_GEOMETRY     = 404,  /* Monitor geometry changed */
  EVT_MONITOR_TAG_CHANGED  = 405,  /* Monitor tagset changed */
  EVT_MONITOR_LAYOUT_CHANGED = 406,  /* Monitor layout changed */
  EVT_MONITOR_FOCUSED    = 407,  /* Monitor gained focus */
  EVT_MONITOR_UNFOCUSED  = 408,  /* Monitor lost focus */
};

/*
 * Layout names (for display in bar)
 */
extern const char* layout_names[LAYOUT_COUNT];

#endif /* _WM_MONITOR_H_ */
