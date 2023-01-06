#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>

#include "wm-log.h"
#include "wm-xcb.h"
#include "wm-xcb-ewmh.h"

xcb_ewmh_connection_t* ewmh;

void setup_ewmh() {
	xcb_generic_error_t* error;
	ewmh = malloc(sizeof(xcb_ewmh_connection_t));
	memset(ewmh, 0, sizeof(xcb_ewmh_connection_t));
	xcb_intern_atom_cookie_t* cookie = xcb_ewmh_init_atoms(dpy, ewmh);
	if (!xcb_ewmh_init_atoms_replies(ewmh, cookie, &error))
		LOG_FATAL("Failed to initialize EWMH atoms");
	return;
}

void destruct_ewmh() {
	free(ewmh);
}

void print_atom_name(xcb_atom_t atom) {
	xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(dpy, atom);
	xcb_get_atom_name_reply_t* reply = xcb_get_atom_name_reply(dpy, cookie, NULL);
	if (reply == NULL) {
		LOG_ERROR("Failed to get atom name\n");
		return;
	}

	LOG_DEBUG("atom name: %s", xcb_get_atom_name_name(reply));

	free(reply);
}
