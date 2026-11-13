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

#include "src/core/wm-hub.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm.h"

/* Forward declaration */
typedef struct Monitor Monitor;

/*
 * The client's machine-tracked values: the ones with a mutually exclusive state
 * that the WM's behaviour depends on. The definition is in client.c, so a
 * component that assigns one of these fields cannot compile; read and write them
 * through the client_is_*() and client_set_*() accessors below.
 */
typedef struct ClientState ClientState;

/*
 * The bits of ICCCM WM_NORMAL_HINTS this WM reads. The min and max size, the
 * base size and the size increment are what constrains a tiled window.
 * Position and gravity are for windows that place themselves, and aspect is
 * skipped because honouring it in a column leaves part of the area empty: a
 * client that needs it gets its tiling size unchanged.
 */
enum ClientSizeHintFlags {
  CLIENT_SIZE_HINT_US_POSITION   = 1 << 0,
  CLIENT_SIZE_HINT_US_SIZE       = 1 << 1,
  CLIENT_SIZE_HINT_P_POSITION    = 1 << 2,
  CLIENT_SIZE_HINT_P_SIZE        = 1 << 3,
  CLIENT_SIZE_HINT_P_MIN_SIZE    = 1 << 4,
  CLIENT_SIZE_HINT_P_MAX_SIZE    = 1 << 5,
  CLIENT_SIZE_HINT_P_RESIZE_INC  = 1 << 6,
  CLIENT_SIZE_HINT_P_ASPECT      = 1 << 7,
  CLIENT_SIZE_HINT_P_BASE_SIZE   = 1 << 8,
  CLIENT_SIZE_HINT_P_WIN_GRAVITY = 1 << 9,
};

/*
 * WM_NORMAL_HINTS is a fixed xPropSizeHints, not a list of the fields its
 * flags name: 18 CARD32 slots with the values a flag did not set left at zero.
 * Xlib's XSetWMNormalHints() writes all 18 and XGetWMNormalHints() reads each
 * field from its own offset, so a reader that walks the flags lands on the
 * wrong value. Pre-ICCCM clients write the first 15 slots only, which is where
 * base size and gravity would be.
 */
enum ClientSizeHintField {
  CLIENT_SIZE_HINT_FLAGS       = 0,
  CLIENT_SIZE_HINT_X           = 1,
  CLIENT_SIZE_HINT_Y           = 2,
  CLIENT_SIZE_HINT_WIDTH       = 3,
  CLIENT_SIZE_HINT_HEIGHT      = 4,
  CLIENT_SIZE_HINT_MIN_WIDTH   = 5,
  CLIENT_SIZE_HINT_MIN_HEIGHT  = 6,
  CLIENT_SIZE_HINT_MAX_WIDTH   = 7,
  CLIENT_SIZE_HINT_MAX_HEIGHT  = 8,
  CLIENT_SIZE_HINT_WIDTH_INC   = 9,
  CLIENT_SIZE_HINT_HEIGHT_INC  = 10,
  CLIENT_SIZE_HINT_ASPECT_MIN  = 11,
  CLIENT_SIZE_HINT_ASPECT_MAX  = 14,
  CLIENT_SIZE_HINT_BASE_WIDTH  = 15,
  CLIENT_SIZE_HINT_BASE_HEIGHT = 16,
  CLIENT_SIZE_HINT_GRAVITY     = 17,
  CLIENT_SIZE_HINT_OLD_LENGTH  = 15,
  CLIENT_SIZE_HINT_LENGTH      = 18,
};

/*
 * A window's size constraints, as the client asked for them.
 * Zero means the client set no constraint of that kind, which is what ICCCM
 * says a missing field means, so every field is usable without a flag.
 *
 * Base and minimum default to each other when a size is applied: ICCCM 4.1.2.3
 * gives a client that sets only PMinSize that size as its base, and a client
 * that sets only PBaseSize that size as its minimum.
 */
typedef struct ClientSizeHints {
  uint16_t min_width;
  uint16_t min_height;
  uint16_t max_width;
  uint16_t max_height;
  uint16_t base_width;
  uint16_t base_height;
  uint16_t inc_width;
  uint16_t inc_height;
} ClientSizeHints;

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
  char*        instance_name;

  /* Geometry */
  int16_t  x;
  int16_t  y;
  uint16_t width;
  uint16_t height;
  uint16_t border_width;

  /* Size constraints from WM_NORMAL_HINTS, and the read that fetches them.
   * The read is a request whose reply arrives later, so a client is reachable
   * with the defaults for one loop iteration before its hints are known. */
  ClientSizeHints           size_hints;
  bool                      size_hints_pending;
  xcb_get_property_cookie_t size_hints_cookie;

  /* WM_CLASS and WM_NAME decide which window rules match, and both arrive
   * from the server, so a managed client is reachable for a loop iteration
   * with no class, instance or title. The flags say which reads are still in
   * flight. */
  bool                      class_pending;
  bool                      title_pending;
  xcb_get_property_cookie_t class_cookie;
  xcb_get_property_cookie_t title_cookie;

  /* Monitor association */
  Monitor* monitor;

  /* Tags - bitmask of assigned tags */
  uint32_t tags;

  /* Managed, urgent and mapped state. The definition of ClientState is not in
   * this header: see the typedef above. */
  ClientState* tracked;

  /* Can receive focus, from WM_HINTS. Not tracked state: a capability. */
  bool focusable;

  enum xcb_stack_mode_t stack_mode; /* X11 stack mode */

  /* Unmaps the WM asked the server for whose UnmapNotify has not been read
   * yet. A hide and a later show can both run before the loop reads the
   * notify, so the notify cannot be classified from the state it finds. */
  uint32_t pending_unmaps;

  /* Adopted state machines - dynamically allocated on demand */
  struct {
    StateMachine** machines; /* array of SM pointers */
    char**         names;    /* corresponding SM names */
    uint32_t       count;    /* number of SMs */
    uint32_t       capacity; /* allocated capacity */
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
 * Intern the atoms the client code needs but xcb does not predefine.
 *
 * Interning needs a reply from the server, so this runs once at startup,
 * before the event loop. Does nothing when there is no X connection.
 */
void client_atoms_init(void);

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
 * Allocates and initializes the Client, registers it with the Hub, and adds
 * it to the client list. Attaches it to the selected monitor, or to the first
 * monitor when none is selected. The monitor stays NULL when no monitor exists.
 * Tags stay 0 until the tag-manager component sees the client managed and
 * assigns it the monitor's visible tags.
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
 * Get the next client in the list, or NULL at the end.
 *
 * The list is circular around a sentinel: the last client's next points at the
 * sentinel, not at NULL. A loop that follows c->next until NULL never ends.
 * A walk stops at the sentinel, or calls this, which returns NULL there.
 * client_foreach() stops at the sentinel and saves the next pointer before its
 * callback. A callback that may destroy the current client leaves the pointer
 * it held dangling, so save the next one before the callback.
 *
 * No gate checks the walk. A loop that follows c->next until NULL compiles and
 * never ends, and the sanitizer gates see a use after free only when a test
 * reaches that path.
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
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should destroy the SM to avoid leaks.
 */
bool client_set_sm(Client* c, const char* sm_name, StateMachine* sm);

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
 * Instance name
 *
 * The first of the two strings in WM_CLASS, which is what a `float
 * instance=` rule matches. Ownership of `instance_name` is transferred to the
 * client.
 */
void client_set_instance(Client* c, char* instance_name);

/*
 * Geometry
 */
void client_set_geometry(Client* c, int16_t x, int16_t y, uint16_t width, uint16_t height);
void client_set_border_width(Client* c, uint16_t border_width);

/*
 * Size hints
 */

/*
 * Ask the X server for the window's WM_NORMAL_HINTS.
 *
 * The request is sent and its reply read later by client_poll_size_hints(),
 * so this never waits: a handler may call it. Does nothing without an X
 * connection (unit tests) or when the atom was never interned, in which case
 * the client keeps the constraints it has.
 */
void client_request_size_hints(Client* c);

/*
 * Take the WM_NORMAL_HINTS reply for a client if it has arrived.
 *
 * Returns true when the reply was consumed and the constraints it names differ
 * from the ones the client had, so the caller knows the geometry it computed
 * from the old ones is out of date. That covers both directions: a client that
 * raises its minimum and one that drops it or removes the property entirely.
 * Returns false when the reply is not there yet, and when it names what the
 * client already had.
 */
bool client_poll_size_hints(Client* c);

/*
 * Whether two constraint sets are the same. Used by the reader above to tell a
 * reply that changes nothing from one that invalidates the client's geometry.
 */
bool client_size_hints_equal(const ClientSizeHints* a, const ClientSizeHints* b);

/*
 * Window identity
 */

/*
 * Ask the X server for the window's WM_CLASS and WM_NAME.
 *
 * Returns true when at least one reply is still on its way, so a caller that
 * has to decide something from the client's class or title knows the answer is
 * not there yet. The replies are read by client_poll_identity(), so this never
 * waits: a handler may call it.
 *
 * Returns false when there is nothing to wait for: no X connection (unit
 * tests) or a client whose reads were already taken.
 */
bool client_request_identity(Client* c);

/*
 * Take the WM_CLASS and WM_NAME replies for a client once both have arrived.
 *
 * Returns true exactly once per client_request_identity(): on the iteration the
 * last of the two replies was consumed. The client's class, instance and title
 * are then what the window said, and a rule can be matched against them.
 * Returns false while a reply is still missing, and for a client that asked for
 * nothing.
 */
bool client_poll_identity(Client* c);

/*
 * Whether a read above is still on its way, so the client's class, instance and
 * title are not the window's own yet.
 */
bool client_identity_pending(const Client* c);

/*
 * Parse the value of a WM_CLASS property: the window's instance name followed
 * by its class, each NUL-terminated, in one 8-bit property.
 *
 * Returns malloc'd copies in `instance_out` and `class_out`. Either one is NULL
 * when the property held fewer names than that, or ended without a terminator
 * after the one before it. Returns false only when there is no instance name at
 * all - an empty property or one with no terminator - in which case nothing is
 * allocated.
 */
#ifdef WM_HUB_TESTING
/*
 * Apply a WM_CLASS or WM_NAME reply to a client without an X connection. The
 * unit suite has no server to answer the reads; the reply's meaning for the
 * client's names and title is the part worth checking.
 */
void client_identity_reply_apply_for_tests(
    Client* c, bool is_class, xcb_get_property_reply_t* reply);
#endif

bool client_class_names_from_property(
    const char* value, uint32_t length, char** instance_out, char** class_out);

/*
 * Whether `atom` is WM_NORMAL_HINTS, the property a client writes its size
 * constraints into. Returns false when the atom was never interned, which is
 * the case without an X connection (unit tests).
 */
bool client_is_normal_hints_atom(xcb_atom_t atom);

/*
 * Parse the values of a WM_NORMAL_HINTS property into constraints.
 *
 * The values are signed, because a client that writes a negative dimension
 * means it as one and not as a size of 65535. A dimension the client did not
 * set stays at zero. `count` is the number of CARD32 slots: 18 for the format
 * Xlib writes, 15 for the pre-ICCCM one that has no base size. Returns false
 * for any other length, and when a 15-slot property names a base size there is
 * no slot for, in which case `out` is left untouched.
 */
bool client_size_hints_from_property(
    const int32_t* values, uint32_t count, ClientSizeHints* out);

/*
 * Replace a client's constraints. Used by the reader above and by tests that
 * have no X connection to read the property from.
 */
void client_set_size_hints(Client* c, const ClientSizeHints* hints);

/*
 * Narrow a size to what the client's hints allow.
 *
 * Fills in the base and minimum that default to each other, applies the size
 * increment to the part above the base size, then the maximum and then the
 * minimum, which is the order ICCCM implies: a size that respects the
 * increment and still fits the range is what the client can draw in.
 * Sizes are the inside size of the window; the border is added by the WM and is
 * not part of what the hints describe.
 */
void client_apply_size_hints(const Client* c, uint16_t* width, uint16_t* height);

/*
 * Tags (tag must be < 32)
 */
void client_set_tags(Client* c, uint32_t tags);
void client_add_tag(Client* c, uint32_t tag);
void client_remove_tag(Client* c, uint32_t tag);
bool client_has_tag(const Client* c, uint32_t tag);

/*
 * Urgency
 *
 * Urgent, managed and mapped are the values a component must not assign. They
 * live in a definition client.c keeps to itself; these are the only way in.
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
 * Spend one unmap the WM recorded in client_hide(). Returns true when this
 * UnmapNotify answers a hide the WM asked for, which is not a withdrawal.
 */
bool client_take_pending_unmap(Client* c);

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
 * ICCCM WM_STATE values. There is no 2: the protocol skips it.
 */
enum ClientWMState {
  CLIENT_WM_STATE_WITHDRAWN = 0,
  CLIENT_WM_STATE_NORMAL    = 1,
  CLIENT_WM_STATE_ICONIC    = 3,
};

/*
 * Set the ICCCM WM_STATE property on a window. Other clients use it to tell a
 * managed window from one the WM ignores.
 *
 * Returns false when the atom was never interned, which happens when there is
 * no X connection (unit tests); the property is then not set.
 */
bool client_set_wm_state(xcb_window_t wnd, enum ClientWMState state);

/*
 * Show a client window (map it).
 */
xcb_void_cookie_t client_show(xcb_window_t wnd);

/*
 * Hide a client window (unmap it).
 */
xcb_void_cookie_t client_hide(xcb_window_t wnd);

#endif /* _CLIENT_H_ */
