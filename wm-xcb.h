#ifndef _WM_XCB_H_
#define _WM_XCB_H_

#include <stdbool.h>

#include <xcb/xcb.h>

extern xcb_connection_t* dpy;
extern xcb_window_t      root;

void setup_xcb();
void wm_xcb_adopt_existing_windows();
void wm_xcb_wait_for_events();
void handle_xcb_events();
void destruct_xcb();
void error_details(xcb_generic_error_t* error);

/*
 * A descriptor the event loop watches besides the X connection, and the
 * callback it runs when that descriptor is readable.
 *
 * This is the extension point for a module that waits on a child process: it
 * forks, hands the pipe to the loop, and returns. The callback must read until
 * EAGAIN or the descriptor will be readable on the next poll(2) too, and the
 * loop will spin.
 */
typedef void (*WmLoopFdHandler)(int fd, void* userdata);

/*
 * Registers `fd` with the loop. The caller keeps ownership of the descriptor:
 * removing the handler does not close it.
 * Returns false when `fd` is invalid, already registered, or out of memory.
 */
bool wm_loop_add_fd_handler(int fd, WmLoopFdHandler handler, void* userdata);

/* Stops watching `fd`. Does nothing when it is not registered. */
void wm_loop_remove_fd_handler(int fd);

/*
 * Runs the handler of every registered descriptor that is readable now,
 * without waiting. Returns how many handlers ran. A handler may remove a
 * descriptor, including its own; the others are still called.
 */
int wm_loop_dispatch_fd_handlers(void);

/*
 * Closes the X connection's socket in a freshly forked child. A child that
 * keeps a copy of the socket alive leaves the server believing the WM is still
 * connected after it has gone, with its grabs and its managed windows in place.
 */
void wm_xcb_close_connection_in_child(void);

#endif
