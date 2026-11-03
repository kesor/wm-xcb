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
 *  60 - 63  reserved
 */

#ifndef _EVENT_TYPES_H_
#define _EVENT_TYPES_H_

#include "wm-hub.h"

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

_Static_assert(EVT_CLIENT_LIST_LIMIT <= EVT_FULLSCREEN_BASE, "client-list range overlaps fullscreen");
_Static_assert(EVT_FULLSCREEN_LIMIT <= EVT_TAG_MANAGER_BASE, "fullscreen range overlaps tag-manager");
_Static_assert(EVT_TAG_MANAGER_LIMIT <= EVT_FOCUS_BASE, "tag-manager range overlaps focus");
_Static_assert(EVT_FOCUS_LIMIT <= EVT_TILING_BASE, "focus range overlaps tiling");
_Static_assert(EVT_TILING_LIMIT <= EVT_CONNECTION_BASE, "tiling range overlaps connection");
_Static_assert(EVT_CONNECTION_LIMIT <= HUB_MAX_EVENT_TYPES, "connection range exceeds the event bus");

#endif /* _EVENT_TYPES_H_ */
