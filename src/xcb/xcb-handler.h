#ifndef _WM_XCB_HANDLER_H_
#define _WM_XCB_HANDLER_H_

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration - actual definition comes from wm-hub.h */
typedef struct HubComponent HubComponent;

/*
 * XCB Event type - using uint8_t to match XCB response_type field
 * Standard XCB events are 1-127, with 0 indicating errors
 */
typedef uint8_t XCBEventType;

/*
 * Event type constants - XCB values (no collisions)
 * See: https://xcb.freedesktop.org/tutorial/events/
 */
#define XCB_EVENT_TYPE_KEY_PRESS          2
#define XCB_EVENT_TYPE_KEY_RELEASE        3
#define XCB_EVENT_TYPE_BUTTON_PRESS       4
#define XCB_EVENT_TYPE_BUTTON_RELEASE     5
#define XCB_EVENT_TYPE_MOTION_NOTIFY      6
#define XCB_EVENT_TYPE_ENTER_NOTIFY       7
#define XCB_EVENT_TYPE_LEAVE_NOTIFY       8
#define XCB_EVENT_TYPE_FOCUS_IN           9
#define XCB_EVENT_TYPE_FOCUS_OUT         10
#define XCB_EVENT_TYPE_KEYMAP_NOTIFY     11
#define XCB_EVENT_TYPE_EXPOSE            12
#define XCB_EVENT_TYPE_GRAPHICS_EXPOSURE 16
#define XCB_EVENT_TYPE_NO_EXPOSURE       17
#define XCB_EVENT_TYPE_VISIBILITY_NOTIFY 18
#define XCB_EVENT_TYPE_CREATE_NOTIFY     20
#define XCB_EVENT_TYPE_DESTROY_NOTIFY    21
#define XCB_EVENT_TYPE_UNMAP_NOTIFY      22
#define XCB_EVENT_TYPE_MAP_NOTIFY        23
#define XCB_EVENT_TYPE_MAP_REQUEST       24
#define XCB_EVENT_TYPE_REPARENT_NOTIFY   25
#define XCB_EVENT_TYPE_CONFIGURE_NOTIFY  26
#define XCB_EVENT_TYPE_CONFIGURE_REQUEST 27
#define XCB_EVENT_TYPE_GRAVITY_NOTIFY    28
#define XCB_EVENT_TYPE_RESIZE_REQUEST    30
#define XCB_EVENT_TYPE_CIRCULATE_NOTIFY  31
#define XCB_EVENT_TYPE_CIRCULATE_REQUEST 32
#define XCB_EVENT_TYPE_PROPERTY_NOTIFY   28
#define XCB_EVENT_TYPE_SELECTION_CLEAR   30
#define XCB_EVENT_TYPE_SELECTION_REQUEST 31
#define XCB_EVENT_TYPE_SELECTION_NOTIFY  33
#define XCB_EVENT_TYPE_COLORMAP_NOTIFY   34
#define XCB_EVENT_TYPE_CLIENT_MESSAGE    35
#define XCB_EVENT_TYPE_MAPPING_NOTIFY    36
#define XCB_EVENT_TYPE_GE_GENERIC        35
#define XCB_EVENT_TYPE_ERROR              0

/*
 * Handler structure - stores registration info
 */
typedef struct XCBHandler {
  XCBEventType      event_type;
  HubComponent*     component;
  void (*handler)(void* event);
  struct XCBHandler* next;
} XCBHandler;

/*
 * Opaque XCB event type - components receive void* and cast as needed
 */
typedef void XCBGenericEvent;

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
 * @param event_type   XCB event type (e.g., XCB_EVENT_TYPE_KEY_PRESS)
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

/*
 * Event type constants for external use.
 * Only provide these as fallbacks when XCB's headers have not already
 * defined the standard XCB_* event macros.
 */
#ifndef XCB_KEY_PRESS
#define XCB_KEY_PRESS       XCB_EVENT_TYPE_KEY_PRESS
#endif
#ifndef XCB_KEY_RELEASE
#define XCB_KEY_RELEASE     XCB_EVENT_TYPE_KEY_RELEASE
#endif
#ifndef XCB_BUTTON_PRESS
#define XCB_BUTTON_PRESS    XCB_EVENT_TYPE_BUTTON_PRESS
#endif
#ifndef XCB_BUTTON_RELEASE
#define XCB_BUTTON_RELEASE  XCB_EVENT_TYPE_BUTTON_RELEASE
#endif
#ifndef XCB_MOTION_NOTIFY
#define XCB_MOTION_NOTIFY   XCB_EVENT_TYPE_MOTION_NOTIFY
#endif
#ifndef XCB_ENTER_NOTIFY
#define XCB_ENTER_NOTIFY    XCB_EVENT_TYPE_ENTER_NOTIFY
#endif
#ifndef XCB_LEAVE_NOTIFY
#define XCB_LEAVE_NOTIFY    XCB_EVENT_TYPE_LEAVE_NOTIFY
#endif
#ifndef XCB_FOCUS_IN
#define XCB_FOCUS_IN        XCB_EVENT_TYPE_FOCUS_IN
#endif
#ifndef XCB_FOCUS_OUT
#define XCB_FOCUS_OUT       XCB_EVENT_TYPE_FOCUS_OUT
#endif
#ifndef XCB_EXPOSE
#define XCB_EXPOSE          XCB_EVENT_TYPE_EXPOSE
#endif
#ifndef XCB_CREATE_NOTIFY
#define XCB_CREATE_NOTIFY   XCB_EVENT_TYPE_CREATE_NOTIFY
#endif
#ifndef XCB_DESTROY_NOTIFY
#define XCB_DESTROY_NOTIFY  XCB_EVENT_TYPE_DESTROY_NOTIFY
#endif
#ifndef XCB_UNMAP_NOTIFY
#define XCB_UNMAP_NOTIFY    XCB_EVENT_TYPE_UNMAP_NOTIFY
#endif
#ifndef XCB_MAP_NOTIFY
#define XCB_MAP_NOTIFY      XCB_EVENT_TYPE_MAP_NOTIFY
#endif
#ifndef XCB_MAP_REQUEST
#define XCB_MAP_REQUEST     XCB_EVENT_TYPE_MAP_REQUEST
#endif
#ifndef XCB_PROPERTY_NOTIFY
#define XCB_PROPERTY_NOTIFY  XCB_EVENT_TYPE_PROPERTY_NOTIFY
#endif
#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC      XCB_EVENT_TYPE_GE_GENERIC
#endif

#endif /* _WM_XCB_HANDLER_H_ */