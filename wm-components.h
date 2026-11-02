#ifndef _WM_COMPONENTS_H_
#define _WM_COMPONENTS_H_

/*
 * Start and stop the built-in components in the order the window manager
 * needs. wm.c calls these around the event loop, and the unit tests call them
 * too, so a component that drops out of the order fails a test instead of
 * quietly never being registered at runtime.
 */
void wm_components_init(void);
void wm_components_shutdown(void);

#endif
