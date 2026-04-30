#ifndef _WM_XCB_EWMH_H_
#define _WM_XCB_EWMH_H_

#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

/* EWMH connection global - defined in wm-xcb-ewmh.c */
extern xcb_ewmh_connection_t* ewmh;

void setup_ewmh();
void destruct_ewmh();
void print_atom_name(xcb_atom_t atom);

/*
 * Check if _NET_WM_STATE contains _NET_WM_STATE_FULLSCREEN for a window.
 */
bool ewmh_check_wm_state_fullscreen(xcb_ewmh_connection_t* ewmh, xcb_window_t window);

#endif
