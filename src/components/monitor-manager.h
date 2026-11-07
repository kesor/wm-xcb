/*
 * Monitor Manager Component
 *
 * Handles display detection via RandR.
 * - Registers XCB handler for RANDR_NOTIFY events
 * - Creates/destroys Monitor targets based on output connect/disconnect
 * - Manages Monitor lifecycle and Hub registration
 * - Performs initial RandR output discovery on startup
 */

#ifndef _MONITOR_MANAGER_H_
#define _MONITOR_MANAGER_H_

#include "../xcb/xcb-handler.h"
#include "src/core/wm-hub.h"

/*
 * Monitor Manager Component
 *
 * Handles display detection via RandR.
 * Listens to XCB_RANDR_NOTIFY events and manages Monitor targets.
 */
#define MONITOR_MANAGER_COMPONENT_NAME "monitor-manager"

extern HubComponent monitor_manager_component;

/*
 * Initialize the monitor manager.
 * - Registers XCB handler for RANDR events
 * - Discovers all currently connected RandR outputs (best-effort; skipped
 *   without X connection or root window)
 * - Creates Monitor targets for each connected output
 *
 * This function is idempotent - multiple calls are safe.
 */
void monitor_manager_init(void);

/*
 * Check if the component is initialized (for the startup check)
 */
bool monitor_manager_is_initialized(void);

/*
 * Shutdown the monitor manager.
 * - Unregisters XCB handler
 *
 * Note: Monitor destruction is handled by monitor_list_shutdown()
 * which is called separately during WM shutdown.
 */
void monitor_manager_shutdown(void);

/*
 * Handle a RandR notify event.
 * Called by the XCB handler dispatcher when RandR events occur.
 */
void monitor_manager_handle_randr_notify(void* event);

/*
 * Get the monitor manager component.
 * Convenience function for registration.
 */
HubComponent* monitor_manager_get_component(void);

#ifdef WM_HUB_TESTING
/*
 * True while the async RandR pass is waiting on a reply. A unit test has no X
 * connection, so the pass cannot start there; asserting this tells a test which
 * branch it actually entered instead of letting it read a fallback as coverage.
 */
bool monitor_manager_pass_running_for_tests(void);
#endif

#endif /* _MONITOR_MANAGER_H_ */