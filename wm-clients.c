#include <xcb/xcb.h>

#include "wm-xcb.h"
#include "wm-clients.h"

/**
 * Change the position, size, border width, and stack order of a client window.
 *
 * This function updates the specified window with the given parameters and
 * sends a request to the X server to perform the update. The window will be
 * updated asynchronously, and the function will return immediately.
 *
 * @param wnd          The window to be updated. This must be a valid window.
 * @param x            The new x coordinate of the window.
 * @param y            The new y coordinate of the window.
 * @param width        The new width of the window.
 * @param height       The new heigth of the window.
 * @param border_width The new border width of the window.
 * @param stack_mode   The new stack mode of the window. This should be either
 *                     \c XCB_STACK_MODE_ABOVE or \c XCB_STACK_MODE_BELOW.
 *
 * @returns The cookie from the \c xcb_configure_window() call.
 *
 * @example
 * client_configure(wnd, 50, 50, 200, 200, 1, XCB_STACK_MODE_ABOVE);
 *
 * @see xcb_configure_window
*/
xcb_void_cookie_t client_configure(
	xcb_window_t wnd,
	int16_t      x,
	int16_t      y,
	uint16_t     width,
	uint16_t     height,
	uint16_t     border_width,
	enum xcb_stack_mode_t stack_mode
) {
	// Prepare the value list for the xcb_configure_window() call.
	uint32_t value_list[] = { x, y, width, height, border_width, stack_mode };

	// Set the value mask to indicate which window parameters should be updated.
	const uint16_t value_mask = 0
		| XCB_CONFIG_WINDOW_X
		| XCB_CONFIG_WINDOW_Y
		| XCB_CONFIG_WINDOW_WIDTH
		| XCB_CONFIG_WINDOW_HEIGHT
		| XCB_CONFIG_WINDOW_BORDER_WIDTH
		| XCB_CONFIG_WINDOW_STACK_MODE;

	// Send the request to the X server to update the window.
	return xcb_configure_window(dpy, wnd, value_mask, value_list);
}

/**
 * Show a client window.
 *
 * This function sends a request to the X server to map (show) the specified
 * window. The window will be displayed asynchronously, and the function will
 * return immediately.
 *
 * @param wnd The window to be shown. This must be a valid window.
 *
 * @returns The cookie from the \c xcb_map_window() call.
 *
 * @see xcb_map_window
 */
xcb_void_cookie_t client_show(xcb_window_t wnd) {
	return xcb_map_window(dpy, wnd);
}


/**
 * Hide a client window.
 *
 * This function sends a request to the X server to unmap (hide) the specified
 * window. The window will be hidden asynchronously, and the function will
 * return immediately.
 *
 * @param wnd The window to be hidden. This must be a valid window.
 *
 * @returns The cookie from the \c xcb_unmap_window() call.
 *
 * @see xcb_unmap_window
 */
xcb_void_cookie_t client_hide(xcb_window_t wnd) {
	return xcb_unmap_window(dpy, wnd);
}
