#ifndef _WM_XCB_
#define _WM_XCB_

#include <xcb/xcb.h>

extern xcb_connection_t* dpy;
extern xcb_window_t root;

void setup_xcb();
void handle_xcb_events();
void destruct_xcb();

#endif
