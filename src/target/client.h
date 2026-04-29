/*
 * Client Target - X11 window entity for the window manager
 *
 * A Client represents a managed X11 window. It owns:
 * - X properties (window, title, class, geometry)
 * - Monitor and tag associations
 * - Linked list links for window ordering
 * - State machine slots (allocated on demand by components)
 *
 * Uses a sentinel-based circular doubly-linked list for efficient
 * insertion, removal, and iteration.
 *
 * The Client registers with the Hub as TARGET_TYPE_CLIENT.
 */

#ifndef _CLIENT_H_
#define _CLIENT_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "wm-hub.h"
#include "../sm/sm-registry.h"
#include "../sm/sm.h"

/* Forward declaration */
typedef struct Monitor Monitor;

/*
 * Client structure
 * Represents a managed X11 window in the window manager.
 */
typedef struct Client {
  /* Base target for Hub registration */
  HubTarget target;

  /* X11 window properties */
  xcb_window_t window;
  char*        title;
  char*        class_name;

  /* Geometry */
  int16_t  x;
  int16_t  y;
  uint16_t width;
  uint16_t height;
  uint16_t border_width;

  /* Monitor association */
  Monitor* monitor;

  /* Tags - bitmask of assigned tags */
  uint32_t tags;

  /* Managed state */
  bool                  managed;    /* added to window list */
  bool                  urgent;     /* has urgency hint */
  bool                  focusable;  /* can receive focus */
  bool                  mapped;     /* is currently mapped */
  enum xcb_stack_mode_t stack_mode; /* X11 stack mode */

  /* Adopted state machines - dynamically allocated on demand */
  struct {
    StateMachine* fullscreen;
    StateMachine* floating;
    StateMachine* urgency;
    StateMachine* focus;
  } sms;

  /* Linked list links (sentinel-based circular list) */
  struct Client* next;
  struct Client* prev;

} Client;

/*
 * Client List Lifecycle
 */

/*
 * Initialize the client list.
 * Must be called before using any client list functions.
 */
void client_list_init(void);

/*
 * Shutdown the client list.
 * Destroys all clients in the list and cleans up resources.
 */
void client_list_shutdown(void);

/*
 * Get the client list sentinel.
 * The sentinel marks both the head and tail of the circular list.
 */
Client* client_list_sentinel(void);

/*
 * Get the first client in the list.
 * Returns NULL if the list is empty.
 */
Client* client_list_get_head(void);

/*
 * Check if the client list is empty.
 */
bool client_list_is_empty(void);

/*
 * Count clients in the list.
 */
uint32_t client_list_count(void);

/*
 * Check if a window exists in the list.
 */
bool client_list_contains_window(xcb_window_t window);

/*
 * Client Lifecycle
 */

/*
 * Create a new Client for an X11 window.
 * Allocates and initializes the Client, registers with Hub,
 * and adds to the client list.
 *
 * Returns NULL on allocation failure or if window already exists.
 */
Client* client_create(xcb_window_t window);

/*
 * Destroy a Client.
 * Detaches from monitor, destroys all state machines,
 * unregisters from Hub, and frees all resources.
 */
void client_destroy(Client* c);

/*
 * Destroy a client by its window ID.
 */
void client_destroy_by_window(xcb_window_t window);

/*
 * Get a client by its window ID from the Hub registry.
 */
Client* client_get_by_window(xcb_window_t window);

/*
 * Get the next client in the list.
 */
Client* client_get_next(const Client* c);

/*
 * Get the previous client in the list.
 */
Client* client_get_prev(const Client* c);

/*
 * Iterate over all clients.
 * Safe against callback removing the current client.
 */
void client_foreach(void (*callback)(Client*));

/*
 * Iterate over all clients in reverse order.
 * Safe against callback removing the current client.
 */
void client_foreach_reverse(void (*callback)(Client*));

/*
 * Count the number of managed clients.
 */
uint32_t client_count_managed(void);

/*
 * State Machine Management
 */

/*
 * Get a state machine attached to this client by name.
 *
 * Returns the StateMachine pointer stored via client_set_sm(), or
 * NULL if no state machine has been attached for the given name.
 */
StateMachine* client_get_sm(Client* c, const char* sm_name);

/*
 * Set a state machine for this client.
 * Used by components to attach their SM templates.
 */
void client_set_sm(Client* c, const char* sm_name, StateMachine* sm);

/*
 * Client Property Accessors
 */

/*
 * Focus
 */
bool client_is_focusable(const Client* c);
void client_set_focusable(Client* c, bool focusable);

/*
 * Monitor association
 */
void     client_set_monitor(Client* c, Monitor* m);
Monitor* client_get_monitor(const Client* c);

/*
 * Title
 *
 * Ownership of `title` is transferred to the client.
 * Do not pass string literals or stack-allocated buffers.
 */
void client_set_title(Client* c, char* title);

/*
 * Class name
 *
 * Ownership of `class_name` is transferred to the client.
 * Do not pass string literals or stack-allocated buffers.
 */
void client_set_class(Client* c, char* class_name);

/*
 * Geometry
 */
void client_set_geometry(Client* c, int16_t x, int16_t y, uint16_t width, uint16_t height);
void client_set_border_width(Client* c, uint16_t border_width);

/*
 * Tags (tag must be < 32)
 */
void client_set_tags(Client* c, uint32_t tags);
void client_add_tag(Client* c, uint32_t tag);
void client_remove_tag(Client* c, uint32_t tag);
bool client_has_tag(const Client* c, uint32_t tag);

/*
 * Urgency
 */
void client_set_urgent(Client* c, bool urgent);
bool client_is_urgent(const Client* c);

/*
 * Managed state
 */
void client_set_managed(Client* c, bool managed);
bool client_is_managed(const Client* c);

/*
 * Mapped state
 */
void client_set_mapped(Client* c, bool mapped);
bool client_is_mapped(const Client* c);

/*
 * Stack mode
 */
void                  client_set_stack_mode(Client* c, enum xcb_stack_mode_t stack_mode);
enum xcb_stack_mode_t client_get_stack_mode(const Client* c);

/*
 * XCB Utility Functions
 * Wrappers for XCB operations on client windows.
 */

/*
 * Configure a client window (position, size, border, stack order).
 */
xcb_void_cookie_t client_configure(
    xcb_window_t          wnd,
    int16_t               x,
    int16_t               y,
    uint16_t              width,
    uint16_t              height,
    uint16_t              border_width,
    enum xcb_stack_mode_t stack_mode);

/*
 * Configure a client from its struct.
 */
xcb_void_cookie_t client_configure_from_struct(const Client* c);

/*
 * Show a client window (map it).
 */
xcb_void_cookie_t client_show(xcb_window_t wnd);

/*
 * Hide a client window (unmap it).
 */
xcb_void_cookie_t client_hide(xcb_window_t wnd);

#endif /* _CLIENT_H_ */
