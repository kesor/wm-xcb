#ifndef _WM_XCB_H_
#define _WM_XCB_H_

#include <xcb/xcb.h>

extern xcb_connection_t* dpy;
extern xcb_window_t root;

void setup_xcb();
void handle_xcb_events();
void destruct_xcb();
void error_details(xcb_generic_error_t* error);

#endif
