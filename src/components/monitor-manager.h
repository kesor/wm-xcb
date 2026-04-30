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
#include "wm-hub.h"

/*
 * Monitor Manager Component
 *
 * Handles display detection via RandR.
 * Listens to XCB_RANDR_NOTIFY events and manages Monitor targets.
 */
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

#endif /* _MONITOR_MANAGER_H_ */