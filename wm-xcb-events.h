#ifndef _WM_XCB_EVENTS_H_
#define _WM_XCB_EVENTS_H_

#include <xcb/xcb.h>

void handle_property_notify(xcb_property_notify_event_t* event);
void handle_create_notify(xcb_create_notify_event_t* event);
void handle_destroy_notify(xcb_destroy_notify_event_t* event);
void handle_map_request(xcb_map_request_event_t* event);
void handle_button_press_release(xcb_button_press_event_t* event);
void handle_key_press_release(xcb_key_press_event_t* event);
void handle_enter_notify(xcb_enter_notify_event_t* event);
void handle_leave_notify(xcb_leave_notify_event_t* event);
void handle_motion_notify(xcb_motion_notify_event_t* event);
void handle_keymap_notify(xcb_keymap_notify_event_t* event);
void handle_unmap_notify(xcb_unmap_notify_event_t* event);
void handle_map_notify(xcb_map_notify_event_t* event);
void handle_reparent_notify(xcb_reparent_notify_event_t* event);
void handle_expose(xcb_expose_event_t* event);
void handle_ge_generic(xcb_ge_generic_event_t* event);
void handle_xinput_event(xcb_ge_generic_event_t* event);
void handle_xinput_raw_event(xcb_ge_generic_event_t* event);

#endif
