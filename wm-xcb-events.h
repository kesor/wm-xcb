#ifndef _WM_XCB_EVENTS_
#define _WM_XCB_EVENTS_

#include <xcb/xcb.h>

void handle_property_notify(xcb_property_notify_event_t* event);
void handle_create_notify(xcb_create_notify_event_t* event);
void handle_destroy_notify(xcb_destroy_notify_event_t* event);
void handle_map_request(xcb_map_request_event_t* event);

#endif
