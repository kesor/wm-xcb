#include <xcb/xcb.h>

#include "wm-log.h"
#include "wm-xcb.h"
#include "wm-xcb-ewmh.h"

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
