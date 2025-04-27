#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include "wm-log.h"
#include "wm-xcb.h"
#include "wm-xcb-ewmh.h"
#include "wm-window-list.h"

// #define PRINT_EVENT(pFormat,...) LOG_CLEAN("EVENT: "pFormat,##__VA_ARGS__)
#define PRINT_EVENT(pFormat,...)

void handle_property_notify(xcb_property_notify_event_t* event) {
	PRINT_EVENT("Received a 'property notify' event with the following:\n\tsequence: %d\n\twindow: %d\n\tatom: %d\n\ttime: %d\n\tstate: %d",
		event->sequence,
		event->window,
		event->atom,
		event->time,
		event->state
	);
	print_atom_name(event->atom);
	return;
}

void handle_create_notify(xcb_create_notify_event_t* event) {
	PRINT_EVENT("Received a 'create notify' event with the following:\n\tsequence: %d\n\tparent: %d\n\twindow: %d\n\tx: %d\n\ty: %d\n\twidth: %d\n\theight: %d\n\tborder_width: %d\n\toverride_redirect: %d",
		event->sequence,
		event->parent,
		event->window,
		event->x,
		event->y,
		event->width,
		event->height,
		event->border_width,
		event->override_redirect
	);
	window_insert(event->window);
}

void handle_destroy_notify(xcb_destroy_notify_event_t* event) {
	PRINT_EVENT("Received a 'destroy notify' event with the following:\n\tsequence: %d\n\tevent: %d\n\twindow: %d",
		event->sequence,
		event->event,
		event->window
	);
	window_remove(event->window);
}

void handle_map_request(xcb_map_request_event_t* event) {
	PRINT_EVENT("Received a 'map request' event with the following:\n\tsequence: %d\n\tparent: %d\n\twindow: %d",
		event->sequence,
		event->parent,
		event->window
	);
}

void handle_button_press_release(xcb_button_press_event_t* event) {
	PRINT_EVENT("Received a 'button press/release' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tsame_screen: %d",
		event->detail,
		event->sequence,
		event->time,
		event->root,
		event->event,
		event->child,
		event->root_x,
		event->root_y,
		event->event_x,
		event->event_y,
		event->state,
		event->same_screen
	);
}

void handle_key_press_release(xcb_key_press_event_t* event) {
	PRINT_EVENT("Received a 'key press/release' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tsame_screen: %d",
		event->detail,
		event->sequence,
		event->time,
		event->root,
		event->event,
		event->child,
		event->root_x,
		event->root_y,
		event->event_x,
		event->event_y,
		event->state,
		event->same_screen
	);
}

const char* xinput_event_type_name(uint16_t event_type) {
	const char* input_event_type_names[XCB_INPUT_SEND_EXTENSION_EVENT + 1] = {
	"XCB_INPUT_DEVICE_CHANGED",
	"XCB_INPUT_KEY_PRESS",
	"XCB_INPUT_KEY_RELEASE",
	"XCB_INPUT_BUTTON_PRESS",
	"XCB_INPUT_BUTTON_RELEASE",
	"XCB_INPUT_MOTION",
	"XCB_INPUT_ENTER",
	"XCB_INPUT_LEAVE",
	"XCB_INPUT_FOCUS_IN",
	"XCB_INPUT_FOCUS_OUT",
	"XCB_INPUT_HIERARCHY",
	"XCB_INPUT_PROPERTY",
	"XCB_INPUT_RAW_KEY_PRESS",
	"XCB_INPUT_RAW_KEY_RELEASE",
	"XCB_INPUT_RAW_BUTTON_PRESS",
	"XCB_INPUT_RAW_BUTTON_RELEASE",
	"XCB_INPUT_RAW_MOTION",
	"XCB_INPUT_TOUCH_BEGIN",
	"XCB_INPUT_TOUCH_UPDATE",
	"XCB_INPUT_TOUCH_END",
	"XCB_INPUT_TOUCH_OWNERSHIP",
	"XCB_INPUT_RAW_TOUCH_BEGIN",
	"XCB_INPUT_RAW_TOUCH_UPDATE",
	"XCB_INPUT_RAW_TOUCH_END",
	"XCB_INPUT_BARRIER_HIT",
	"XCB_INPUT_BARRIER_LEAVE",
	"XCB_INPUT_SEND_EXTENSION_EVENT",
	};
	if (event_type <= sizeof(input_event_type_names) / sizeof(input_event_type_names[0]))
		return input_event_type_names[event_type];
	return "INVALID";
}

void handle_xinput_event(xcb_ge_generic_event_t* event) {
	xcb_input_key_press_event_t* xi_event = (xcb_input_key_press_event_t*)event;
	LOG_DEBUG("Received '%s' event with the following:\n\tdeviceid: %d\n\ttime: %d\n\tdetail: %d\n\troot wnd: %d\n\tevent wnd: %d\n\tchild wnd: %d\n\tfull_sequence: %d\n\troot_x: %d.%d\n\troot_y: %d.%d\n\tevent_x: %d.%d\n\tevent_y: %d.%d\n\tbuttons_len: %d\n\tvaluators_len: %d\n\tsourceid: %d\n\tflags: %d\n\tmods: %d\n\tgroup: %d",
		xinput_event_type_name(xi_event->event_type),
		xi_event->deviceid,
		xi_event->time,
		xi_event->detail,
		xi_event->root,
		xi_event->event,
		xi_event->child,
		xi_event->full_sequence,
		xi_event->root_x >> 16, xi_event->root_x & 0x0000FFFF,
		xi_event->root_y >> 16, xi_event->root_y & 0x0000FFFF,
		xi_event->event_x >> 16, xi_event->event_x & 0x0000FFFF,
		xi_event->event_y >> 16, xi_event->event_y & 0x0000FFFF,
		xi_event->buttons_len,
		xi_event->valuators_len,
		xi_event->sourceid, // actual device used to trigger event, unlike deviceid which is the "master" device.
		xi_event->flags,
		xi_event->mods,
		xi_event->group
	);
}

void handle_xinput_raw_event(xcb_ge_generic_event_t* event) {
	xcb_input_raw_button_press_event_t* xi_event = (xcb_input_raw_button_press_event_t*)event;
	LOG_DEBUG("Received '%s' event with the following:\n\tdeviceid: %d\n\ttime: %d\n\tdetail: %d\n\tsourceid: %d\n\tvaluators_len: %d\n\tflags: %d\n\tfull_sequence: %d",
		xinput_event_type_name(xi_event->event_type),
		xi_event->deviceid,
		xi_event->time,
		xi_event->detail,
		xi_event->sourceid, // actual device used to trigger event, unlike deviceid which is the "master" device.
		xi_event->valuators_len,
		xi_event->flags,
		xi_event->full_sequence);
}

void handle_enter_notify(xcb_enter_notify_event_t* event) {
	PRINT_EVENT("Received an 'enter notify' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tmode: %d\n\tsame_screen_focus: %d",
		event->detail,
		event->sequence,
		event->time,
		event->root,
		event->event,
		event->child,
		event->root_x,
		event->root_y,
		event->event_x,
		event->event_y,
		event->state,
		event->mode,
		event->same_screen_focus
	);
}

void handle_leave_notify(xcb_leave_notify_event_t* event) {
	PRINT_EVENT("Received a 'leave notify' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tmode: %d\n\tsame_screen_focus: %d",
		event->detail,
		event->sequence,
		event->time,
		event->root,
		event->event,
		event->child,
		event->root_x,
		event->root_y,
		event->event_x,
		event->event_y,
		event->state,
		event->mode,
		event->same_screen_focus
	);
}

void handle_motion_notify(xcb_motion_notify_event_t* event) {
	PRINT_EVENT("Received a 'motion notify' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tsame_screen: %d",
		event->detail,
		event->sequence,
		event->time,
		event->root,
		event->event,
		event->child,
		event->root_x,
		event->root_y,
		event->event_x,
		event->event_y,
		event->state,
		event->same_screen
	);
}

void handle_keymap_notify(xcb_keymap_notify_event_t* event) {
	PRINT_EVENT("Received a 'keymap notify' event with the following:\n\tkeys: %.*s",
		31, event->keys
	);
}

void handle_unmap_notify(xcb_unmap_notify_event_t* event) {
	PRINT_EVENT("Received a 'unmap notify' event with the following:\n\tsequence: %d\n\tevent: %d\n\twindow: %d\n\tfrom_configure: %d",
		event->sequence,
		event->event,
		event->window,
		event->from_configure
	);
}

void handle_map_notify(xcb_map_notify_event_t* event) {
	PRINT_EVENT("Received a 'map notify' event with the following:\n\tsequence: %d\n\tevent: %d\n\twindow: %d\n\toverride_redirect: %d",
		event->sequence,
		event->event,
		event->window,
		event->override_redirect
	);
}

void handle_reparent_notify(xcb_reparent_notify_event_t* event) {
	PRINT_EVENT("Received a 'reparent notify' event with the following:\n\tsequence: %d\n\tevent: %d\n\twindow: %d\n\tparent: %d\n\tx: %d\n\ty: %d\n\toverride_redirect: %d",
		event->sequence,
		event->event,
		event->window,
		event->parent,
		event->x,
		event->y,
		event->override_redirect
	);
	wnd_node_t* wnd = window_insert(event->window);
	wnd->parent = event->parent;
	wnd->x = event->x;
	wnd->y = event->y;
}

void handle_expose(xcb_expose_event_t* event) {
	PRINT_EVENT("Received an 'expose' event with the following:\n\tsequence: %d\n\twindow: %d\n\tx: %d\n\ty: %d\n\twidth: %d\n\theight: %d\n\tcount: %d",
		event->sequence,
		event->window,
		event->x,
		event->y,
		event->width,
		event->height,
		event->count
	);
}

void handle_ge_generic(xcb_ge_generic_event_t* event) {
	PRINT_EVENT("Received a 'ge generic' event with the following:\n\tsequence: %d\n\textension: %d\n\tlength: %d\n\tevent_type: %d\n\tfull_sequence: %d",
		event->sequence,
		event->extension,
		event->length,
		event->event_type,
		event->full_sequence
	);
}
