#ifndef _WM_XCB_EWMH_H_
#define _WM_XCB_EWMH_H_

#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

/* EWMH connection global - defined in wm-xcb-ewmh.c */
extern xcb_ewmh_connection_t* ewmh;

void setup_ewmh();
void destruct_ewmh();

/*
 * Ask for a window's _NET_WM_STATE list. The caller keeps the cookie and reads
 * the reply later: a reply read at the call site stops the WM until the server
 * answers.
 */
xcb_get_property_cookie_t ewmh_request_wm_state(xcb_ewmh_connection_t* ewmh, xcb_window_t window);

/*
 * Check whether a _NET_WM_STATE reply lists _NET_WM_STATE_FULLSCREEN.
 * The reply stays the caller's: it is freed after this returns.
 */
bool ewmh_wm_state_is_fullscreen(xcb_ewmh_connection_t* ewmh, xcb_get_property_reply_t* reply);

#endif
