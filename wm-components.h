#ifndef _WM_COMPONENTS_H_
#define _WM_COMPONENTS_H_

#include <stdbool.h>

/*
 * Start and stop the built-in components in the order the window manager
 * needs. wm.c calls these around the event loop, and the unit tests call them
 * too, so a component that drops out of the order fails a test instead of
 * quietly never being registered at runtime.
 */

/*
 * One module wm_components_init() starts: the name it registers under, and the
 * accessor that says it came up. The list in wm-components.c is the only copy,
 * so the startup test covers a new module by construction.
 */
typedef struct WiredModule {
  const char* name;
  bool (*is_initialized)(void);
} WiredModule;

/*
 * Starts every built-in component and returns false when one of them did not
 * come up, so main() can stop instead of running without it. A component is up
 * when it is registered under its name and its own accessor agrees: an init
 * that returns early - the failure main() used to drop on the floor - fails
 * here.
 */
bool wm_components_init(void);
void wm_components_shutdown(void);

/*
 * The components wm_components_init() starts, NULL-terminated, by the name each
 * registers under.
 */
const WiredModule* wm_components_wired(void);

/*
 * The actions wm_components_init() starts, NULL-terminated, by action name.
 */
const WiredModule* wm_components_wired_actions(void);

#endif
