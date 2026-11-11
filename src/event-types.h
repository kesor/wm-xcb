/*
 * Hub event type ranges
 *
 * The event bus indexes its subscriber table by the event value, and two
 * components that pick the same value receive each other's events. Every
 * component numbers its events from its own range below, so a new component
 * takes the next free range instead of the next free number.
 *
 *   0 -  9  client-list  client lifecycle
 *  10 - 19  fullscreen   fullscreen state transitions
 *  20 - 29  tag-manager  tag view changes
 *  30 - 39  focus        focus state transitions
 *  40 - 49  tiling       layout changes
 *  50 - 59  connection   monitor connect and disconnect
 *  60 - 63  launcher     launcher failure: no selection, or no dmenu
 *  64 - 71  floating     client float and tile transitions
 *  72 - 79  config       the running configuration was replaced
 */

#ifndef _EVENT_TYPES_H_
#define _EVENT_TYPES_H_

#include "src/core/wm-hub.h"

#define EVT_CLIENT_LIST_BASE  0
#define EVT_CLIENT_LIST_LIMIT 10

#define EVT_FULLSCREEN_BASE   10
#define EVT_FULLSCREEN_LIMIT  20

#define EVT_TAG_MANAGER_BASE  20
#define EVT_TAG_MANAGER_LIMIT 30

#define EVT_FOCUS_BASE        30
#define EVT_FOCUS_LIMIT       40

#define EVT_TILING_BASE       40
#define EVT_TILING_LIMIT      50

#define EVT_CONNECTION_BASE   50
#define EVT_CONNECTION_LIMIT  60

#define EVT_LAUNCHER_BASE     60
#define EVT_LAUNCHER_LIMIT    64

#define EVT_FLOATING_BASE     64
#define EVT_FLOATING_LIMIT    72

#define EVT_CONFIG_BASE       72
#define EVT_CONFIG_LIMIT      80

/*
 * Event names that cross a component boundary
 *
 * A component declares its own event names in its own header. An event a
 * second component subscribes to cannot be named that way: the subscriber
 * would include the emitter's header, which docs/PHILOSOPHY.md section 2.1
 * forbids and scripts/check-coupling.sh fails the build on. So a name another
 * component has to spell is declared here, next to the range it belongs to.
 *
 * A name only the emitting component uses stays in that component's header:
 * tiling's EVT_LAYOUT_CHANGED is in tiling.h, focus's EVT_CLIENT_FOCUSED in
 * focus.h.
 */

/* client-list: hub_subscribe()d by focus, tag-manager and ewmh-desktop */
enum ClientListEventType {
  EVT_CLIENT_CREATED   = EVT_CLIENT_LIST_BASE + 0, /* Client was created and registered with hub */
  EVT_CLIENT_DESTROYED = EVT_CLIENT_LIST_BASE + 1, /* Client was destroyed and unregistered with hub */
  EVT_CLIENT_MANAGED   = EVT_CLIENT_LIST_BASE + 2, /* Client was added to the managed client list */
  EVT_CLIENT_UNMANAGED = EVT_CLIENT_LIST_BASE + 3, /* Client was removed from the managed client list */
};

/* fullscreen: hub_subscribe()d by tiling */
enum FullscreenEvent {
  EVT_FULLSCREEN_ENTERED = EVT_FULLSCREEN_BASE + 0, /* Client entered fullscreen */
  EVT_FULLSCREEN_EXITED  = EVT_FULLSCREEN_BASE + 1, /* Client left fullscreen */
  EVT_FULLSCREEN_FAILED  = EVT_FULLSCREEN_BASE + 2, /* The request was refused */
};

/* tag-manager: hub_subscribe()d by ewmh-desktop */
enum TagManagerEventType {
  EVT_TAG_CHANGED = EVT_TAG_MANAGER_BASE + 0, /* Emitted when tag view state changes */
};

/* floating: hub_subscribe()d by tiling */
enum FloatingEvent {
  EVT_CLIENT_FLOATED  = EVT_FLOATING_BASE + 0, /* Client entered floating */
  EVT_CLIENT_TILED    = EVT_FLOATING_BASE + 1, /* Client returned to the layout */
  EVT_FLOATING_FAILED = EVT_FLOATING_BASE + 2, /* The request was refused */
};

_Static_assert(EVT_CLIENT_LIST_LIMIT <= EVT_FULLSCREEN_BASE, "client-list range overlaps fullscreen");
_Static_assert(EVT_FULLSCREEN_LIMIT <= EVT_TAG_MANAGER_BASE, "fullscreen range overlaps tag-manager");
_Static_assert(EVT_TAG_MANAGER_LIMIT <= EVT_FOCUS_BASE, "tag-manager range overlaps focus");
_Static_assert(EVT_FOCUS_LIMIT <= EVT_TILING_BASE, "focus range overlaps tiling");
_Static_assert(EVT_TILING_LIMIT <= EVT_CONNECTION_BASE, "tiling range overlaps connection");
_Static_assert(EVT_CONNECTION_LIMIT <= HUB_MAX_EVENT_TYPES, "connection range exceeds the event bus");
_Static_assert(EVT_CONNECTION_LIMIT <= EVT_LAUNCHER_BASE, "connection range overlaps launcher");
_Static_assert(EVT_LAUNCHER_LIMIT <= EVT_FLOATING_BASE, "launcher range overlaps floating");
_Static_assert(EVT_FLOATING_LIMIT <= HUB_MAX_EVENT_TYPES, "floating range exceeds the event bus");
_Static_assert(EVT_FLOATING_LIMIT <= EVT_CONFIG_BASE, "floating range overlaps config");
_Static_assert(EVT_CONFIG_LIMIT <= HUB_MAX_EVENT_TYPES, "config range exceeds the event bus");

#endif /* _EVENT_TYPES_H_ */
