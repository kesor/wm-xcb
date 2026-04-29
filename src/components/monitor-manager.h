/*
 * Monitor Manager Component
 *
 * Handles display detection via RandR.
 * - Registers XCB handler for RANDR_NOTIFY events
 * - Creates/destroys Monitor targets based on output connect/disconnect
 * - Manages Monitor lifecycle and Hub registration
 *
 * Blocked by:
 * - #9 (XCB handler registration) - already implemented in src/xcb/xcb-handler.c
 * - #15 (Monitor target) - already implemented in src/target/monitor.c
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
 * - Queries existing RandR outputs
 * - Creates Monitor targets for connected outputs
 * - Registers XCB handler for RANDR events
 *
 * Called from wm initialization.
 */
void monitor_manager_init(void);

/*
 * Shutdown the monitor manager.
 * - Unregisters XCB handler
 * - Destroys all Monitor targets
 * - Cleans up resources
 */
void monitor_manager_shutdown(void);

/*
 * Handle a RandR notify event.
 * Called by the XCB handler dispatcher.
 */
void monitor_manager_handle_randr_notify(void* event);

/*
 * Get the monitor manager component.
 * Convenience function for registration.
 */
HubComponent* monitor_manager_get_component(void);

#endif /* _MONITOR_MANAGER_H_ */
