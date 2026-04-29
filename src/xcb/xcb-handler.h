#ifndef _WM_XCB_HANDLER_H_
#define _WM_XCB_HANDLER_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

/* Forward declaration - actual definition comes from wm-hub.h */
typedef struct HubComponent HubComponent;

/*
 * XCB Event type - using uint8_t to match XCB response_type field.
 * Use XCB's event type constants (e.g., XCB_KEY_PRESS, XCB_BUTTON_PRESS).
 */
typedef uint8_t XCBEventType;

/*
 * Handler structure - stores registration info
 */
typedef struct XCBHandler {
  XCBEventType  event_type;
  HubComponent* component;
  void (*handler)(void* event);
  struct XCBHandler* next;
} XCBHandler;

/*
 * Initialize the handler registry.
 * Called once at startup before any component registration.
 */
void xcb_handler_init(void);

/*
 * Shutdown the handler registry.
 * Frees all registered handlers.
 */
void xcb_handler_shutdown(void);

/*
 * Register a handler for an XCB event type.
 *
 * @param event_type   XCB event type (e.g., XCB_KEY_PRESS)
 * @param component    Component owning this handler (for debugging/logging)
 * @param handler      Handler function to call when event occurs
 * @return             0 on success, -1 on failure
 *
 * Multiple handlers can be registered for the same event type.
 * All handlers will be called in registration order.
 */
int xcb_handler_register(XCBEventType event_type, HubComponent* component, void (*handler)(void*));

/*
 * Lookup handlers for an event type.
 *
 * @param event_type   XCB event type to look up
 * @return             Linked list of handlers, or NULL if none registered
 *
 * Note: Returns first handler. Use xcb_handler_next() to iterate.
 */
XCBHandler* xcb_handler_lookup(XCBEventType event_type);

/*
 * Get next handler in chain for same event type.
 * Use after xcb_handler_lookup() to iterate all handlers.
 */
XCBHandler* xcb_handler_next(XCBHandler* handler);

/*
 * Dispatch an event to all registered handlers.
 *
 * @param event        The XCB event to dispatch (void* for type independence)
 *
 * Calls all handlers registered for the event's response_type (with synthetic
 * event flag masked off). The event is NOT freed - caller retains ownership.
 */
void xcb_handler_dispatch(void* event);

/*
 * Unregister all handlers for a component.
 *
 * @param component    Component whose handlers to remove
 *
 * Called during component shutdown.
 */
void xcb_handler_unregister_component(HubComponent* component);

/*
 * Get handler count for debugging.
 */
uint32_t xcb_handler_count(void);

/*
 * Get handler count for specific event type.
 */
uint32_t xcb_handler_count_for_type(XCBEventType event_type);

#endif /* _WM_XCB_HANDLER_H_ */