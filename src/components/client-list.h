/*
 * Client List Component
 *
 * Component for managing the lifecycle of X11 clients (windows).
 * Handles XCB events for client creation, destruction, mapping, and unmapping.
 * Manages client list (sentinel-based circular doubly-linked list).
 *
 * XCB Events Handled:
 * - XCB_CREATE_NOTIFY - create Client, register with hub
 * - XCB_DESTROY_NOTIFY - destroy Client, unregister from hub
 * - XCB_MAP_REQUEST - manage window, add to client list
 * - XCB_UNMAP_NOTIFY - unmanage window, remove from list
 *
 * Events Emitted:
 * - EVT_CLIENT_CREATED - emitted after client creation
 * - EVT_CLIENT_DESTROYED - emitted after client destruction
 * - EVT_CLIENT_MANAGED - emitted when client is managed (added to list)
 * - EVT_CLIENT_UNMANAGED - emitted when client is unmanaged (removed from list)
 *
 * Acceptance Criteria:
 * [x] New window appears -> Client created -> added to list
 * [x] Window destroyed -> Client removed -> list cleaned
 * [x] Client-list component registers handlers at init
 * [x] Client adopts components on creation
 */

#ifndef _COMPONENT_CLIENT_LIST_H_
#define _COMPONENT_CLIENT_LIST_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "wm-hub.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"

/*
 * Component name
 */
#define CLIENT_LIST_COMPONENT_NAME "client-list"

/*
 * Event types for client lifecycle
 * Components subscribe to these to react to client changes.
 */
enum ClientListEventType {
  EVT_CLIENT_CREATED = 0x200,  /* Client was created and registered with hub */
  EVT_CLIENT_DESTROYED,         /* Client was destroyed and unregistered from hub */
  EVT_CLIENT_MANAGED,           /* Client was added to the managed client list */
  EVT_CLIENT_UNMANAGED,         /* Client was removed from the managed client list */
};

/*
 * Client list component structure
 * Provides XCB event handlers and manages the global client list.
 *
 * Note: The component instance is defined in client-list.c.
 * Components access it for registration and handler setup.
 */
typedef struct ClientListComponent {
  /* Base component interface */
  HubComponent base;

  /* Component-specific state */
  bool initialized;
} ClientListComponent;

/*
 * Get the client list component instance.
 * Used by test files to access component state.
 */
ClientListComponent* client_list_component_get(void);

/*
 * Check if component is initialized (for testing)
 */
bool client_list_component_is_initialized(void);

/*
 * Component initialization and shutdown
 */

/*
 * Initialize the client list component.
 * Registers XCB event handlers for:
 * - XCB_CREATE_NOTIFY
 * - XCB_DESTROY_NOTIFY
 * - XCB_MAP_REQUEST
 * - XCB_UNMAP_NOTIFY
 *
 * Must be called before any client lifecycle operations.
 * Returns true on success, false on failure.
 */
bool client_list_component_init(void);

/*
 * Shutdown the client list component.
 * Unregisters XCB handlers and cleans up component state.
 * Does NOT destroy existing clients - call client_list_shutdown first.
 */
void client_list_component_shutdown(void);

/*
 * XCB Event Handlers
 * Called by the xcb_handler dispatch system.
 */

/*
 * Handle XCB_CREATE_NOTIFY event.
 * Creates a new Client for the window and registers with hub.
 */
void client_list_on_create_notify(void* event);

/*
 * Handle XCB_DESTROY_NOTIFY event.
 * Destroys the Client and unregisters from hub.
 */
void client_list_on_destroy_notify(void* event);

/*
 * Handle XCB_MAP_REQUEST event.
 * Manages the window (adds to client list) if not already managed.
 */
void client_list_on_map_request(void* event);

/*
 * Handle XCB_UNMAP_NOTIFY event.
 * Unmanages the window (removes from client list) if managed.
 */
void client_list_on_unmap_notify(void* event);

/*
 * Client lifecycle helpers
 */

/*
 * Emit a client lifecycle event to subscribers.
 */
void client_list_emit_event(enum ClientListEventType type, Client* c);

/*
 * Get the number of currently managed clients.
 */
uint32_t client_list_managed_count(void);

/*
 * Check if a window is managed.
 */
bool client_list_is_managed(xcb_window_t window);

#endif /* _COMPONENT_CLIENT_LIST_H_ */