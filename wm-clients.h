#ifndef _WM_CLIENTS_H_
#define _WM_CLIENTS_H_

#include <xcb/xcb.h>

xcb_void_cookie_t client_configure(
	xcb_window_t wnd,
	int16_t      x,
	int16_t      y,
	uint16_t     width,
	uint16_t     height,
	uint16_t     border_width,
	enum xcb_stack_mode_t stack_mode
);
xcb_void_cookie_t client_show(xcb_window_t wnd);
xcb_void_cookie_t client_hide(xcb_window_t wnd);

#endif
