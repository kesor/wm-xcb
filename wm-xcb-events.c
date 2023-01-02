#include <xcb/xcb.h>

#include "wm-log.h"
#include "wm-xcb.h"
#include "wm-xcb-ewmh.h"
#include "wm-states.h"

void handle_property_notify(xcb_property_notify_event_t* event) {
	LOG_DEBUG("Received a 'property notify' event with the following:\n\tsequence: %d\n\twindow: %d\n\tatom: %d\n\ttime: %d\n\tstate: %d",
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
	LOG_DEBUG("Received a 'create notify' event with the following:\n\tsequence: %d\n\tparent: %d\n\twindow: %d\n\tx: %d\n\ty: %d\n\twidth: %d\n\theight: %d\n\tborder_width: %d\n\toverride_redirect: %d",
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
}

void handle_destroy_notify(xcb_destroy_notify_event_t* event) {
	LOG_DEBUG("Received a 'destroy notify' event with the following:\n\tsequence: %d\n\tevent: %d\n\twindow: %d",
		event->sequence,
		event->event,
		event->window
	);
}

void handle_map_request(xcb_map_request_event_t* event) {
	LOG_DEBUG("Received a 'map request' event with the following:\n\tsequence: %d\n\tparent: %d\n\twindow: %d",
		event->sequence,
		event->parent,
		event->window
	);
}

void handle_button_press_release(xcb_button_press_event_t* event) {
	LOG_DEBUG("Received a 'button press/release' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tsame_screen: %d",
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
	ctx.mouse_buttons |= event->state;
}

void handle_key_press_release(xcb_key_press_event_t* event) {
	LOG_DEBUG("Received a 'key press/release' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tsame_screen: %d",
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

void handle_enter_notify(xcb_enter_notify_event_t* event) {
	LOG_DEBUG("Received an 'enter notify' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tmode: %d\n\tsame_screen_focus: %d",
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
	)
}

void handle_leave_notify(xcb_leave_notify_event_t* event) {
	LOG_DEBUG("Received a 'leave notify' event with the following:\n\tdetail: %d\n\tsequence: %d\n\ttime: %d\n\troot: %d\n\tevent: %d\n\tchild: %d\n\troot_x: %d\n\troot_y: %d\n\tevent_x: %d\n\tevent_y: %d\n\tstate: %d\n\tmode: %d\n\tsame_screen_focus: %d",
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
	)
}
