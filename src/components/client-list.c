/*
 * Client List Component Implementation
 *
 * Handles XCB events for client lifecycle management.
 * Manages the global client list via client.h, and sends REQ_MONITOR_TILE when
 * a window joins or leaves a monitor so the remaining windows are rearranged.
 */

#include <stdlib.h>

#include "src/components/client-list.h"
#include "src/config/config.h"
#include "src/core/wm-log.h"
#include "src/core/wm-xcb.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"

/*
 * Border drawn around a managed window. config.def.h has the same value in
 * default_borders.width, but including it here would copy the whole compiled-in
 * configuration into this file, unused. This is the only place that sets a
 * client's border width: the layout subtracts whatever is on the client, so a
 * change to the configured border has to be made here and in config.def.h.
 */
#define CLIENT_LIST_BORDER_WIDTH 1

/*
 * Global component instance - static initialization ensures clean state
 * on program start. The component tracks its own registration state.
 */
static ClientListComponent client_list_component = {
  .base = {
           .name                  = CLIENT_LIST_COMPONENT_NAME,
           .requests              = NULL,
           .accepted_target_names = (const char*[]) { "client", NULL },
           .accepted_targets      = NULL,
           .executor              = NULL,
           .registered            = false,
           },
  .initialized = false,
};

/* Track if static initialization has occurred */
static bool static_init_done = false;

static void
do_static_init(void)
{
  /* Only run once */
  if (static_init_done)
    return;
  static_init_done = true;
  /* Ensure component is in clean state */
  client_list_component.initialized     = false;
  client_list_component.base.registered = false;
}

/* Called via constructor to ensure static init happens before any test */
__attribute__((constructor)) static void
ensure_static_init(void)
{
  do_static_init();
}

/*
 * Internal helper to reset component state.
 * Called before initialization to ensure clean state.
 * Does not touch hub registration.
 */
static void
client_list_component_reset(void)
{
  client_list_component.initialized = false;
}

/*
 * Get the client list component instance.
 */
ClientListComponent*
client_list_component_get(void)
{
  return &client_list_component;
}

/*
 * Check if component is initialized (for testing)
 */
bool
client_list_component_is_initialized(void)
{
  return client_list_component.initialized;
}

/*
 * Get the base HubComponent for registration.
 * Used internally by init/shutdown functions.
 */
static HubComponent*
client_list_component_base(void)
{
  return &client_list_component.base;
}

/*
 * Component initialization
 */
bool
client_list_component_init(void)
{
  /* Check hub registry first - this is the source of truth for registration.
   * The component's registered flag may be stale if hub_shutdown() was called
   * but component state persisted. */
  HubComponent* existing = hub_get_component_by_name(CLIENT_LIST_COMPONENT_NAME);
  if (existing != NULL) {
    /* Component is already registered in hub */
    if (client_list_component.initialized) {
      LOG_DEBUG("Client list component already initialized and registered");
      return true;
    }
    /* Hub has component but we're not initialized - complete initialization */
    LOG_DEBUG("Component registered with hub, completing initialization");
    client_list_init();
    client_list_component.initialized = true;
    return true;
  }

  /* If we think we're initialized but not in hub, that's inconsistent - reset */
  if (client_list_component.initialized) {
    LOG_DEBUG("Client list component in inconsistent state, resetting");
    client_list_component_reset();
  }

  LOG_DEBUG("Initializing client list component");

  /* Register with hub */
  if (hub_register_component(client_list_component_base()) != 0) {
    client_list_component_reset();
    return false;
  }

  /* Initialize the client list (from client.c) */
  client_list_init();

  /* Register XCB event handlers for client lifecycle events */
  HubComponent* base   = client_list_component_base();
  int           result = 0;

  result |= xcb_handler_register(
      XCB_CREATE_NOTIFY,
      base,
      client_list_on_create_notify);

  result |= xcb_handler_register(
      XCB_DESTROY_NOTIFY,
      base,
      client_list_on_destroy_notify);

  result |= xcb_handler_register(
      XCB_MAP_REQUEST,
      base,
      client_list_on_map_request);

  result |= xcb_handler_register(
      XCB_UNMAP_NOTIFY,
      base,
      client_list_on_unmap_notify);

  result |= xcb_handler_register(
      XCB_PROPERTY_NOTIFY,
      base,
      client_list_on_property_notify);

  if (result != 0) {
    LOG_ERROR("Failed to register some XCB handlers for client list component");
    /* Continue anyway - partial registration is recoverable */
  }

  client_list_component.initialized = true;
  LOG_DEBUG("Client list component initialized successfully");

  return true;
}

/*
 * Component shutdown
 */
void
client_list_component_shutdown(void)
{
  if (!client_list_component.initialized) {
    return;
  }

  LOG_DEBUG("Shutting down client list component");

  /* Unregister XCB handlers for this component */
  xcb_handler_unregister_component(client_list_component_base());

  /* Shutdown the client list (destroys all clients) */
  client_list_shutdown();

  /* Unregister from hub */
  hub_unregister_component(CLIENT_LIST_COMPONENT_NAME);

  client_list_component.initialized = false;
  LOG_DEBUG("Client list component shutdown complete");
}

/*
 * XCB Event Handlers
 */

/*
 * Handle XCB_CREATE_NOTIFY event.
 * Creates a new Client for the window.
 */
void
client_list_on_create_notify(void* event)
{
  xcb_create_notify_event_t* e = (xcb_create_notify_event_t*) event;

  LOG_DEBUG("CREATE_NOTIFY: window=%u, parent=%u, override_redirect=%d",
            e->window, e->parent, e->override_redirect);

  /* Skip override-redirect windows (e.g., popups, menus) */
  if (e->override_redirect) {
    LOG_DEBUG("Skipping override-redirect window %u", e->window);
    return;
  }

  /* Create a new client for this window */
  Client* c = client_create(e->window);
  if (c == NULL) {
    LOG_WARN("Failed to create client for window %u", e->window);
    return;
  }

  /* Set initial geometry from the event */
  client_set_geometry(c, e->x, e->y, e->width, e->height);
  client_set_border_width(c, e->border_width);

  /* Emit client created event */
  client_list_emit_event(EVT_CLIENT_CREATED, c);

  LOG_DEBUG("Client created: window=%u", e->window);
}

/*
 * Ask tiling to rearrange the monitor a client joined or left. The request is
 * what keeps client-list from calling the tiling component directly.
 */
static void
client_list_request_tile(Monitor* m)
{
  if (m == NULL)
    return;

  hub_send_request(REQ_MONITOR_TILE, m->target.id);
}

/*
 * Put a client where the window rules say it belongs.
 *
 * The rules answer whether the client starts out of the layout; the floating
 * state machine is the floating component's, so this sends the request a
 * keybinding sends and lets the transition ask for the re-tile. A client no
 * rule names takes the tiled slot the manage path has always given it.
 */
static void
client_list_place_by_rules(Client* c)
{
  if (config_should_float(c->class_name, c->instance_name, c->title)) {
    LOG_DEBUG("A float rule matches window=%u, it starts out of the layout", c->window);
    hub_send_request(REQ_CLIENT_FLOAT, c->target.id);
    return;
  }

  client_list_request_tile(client_get_monitor(c));
}

/*
 * Handle XCB_DESTROY_NOTIFY event.
 * Destroys the Client for the window.
 */
void
client_list_on_destroy_notify(void* event)
{
  xcb_destroy_notify_event_t* e = (xcb_destroy_notify_event_t*) event;

  LOG_DEBUG("DESTROY_NOTIFY: window=%u", e->window);

  /* Check if we have a client for this window */
  Client* c = client_get_by_window(e->window);
  if (c == NULL) {
    LOG_DEBUG("No client found for destroyed window %u", e->window);
    return;
  }

  /* client_destroy() detaches the client from its monitor, so the monitor has
   * to be saved before the destroy if the remaining windows are to be tiled. */
  Monitor* m = client_get_monitor(c);

  /* Emit client destroyed event BEFORE destroying */
  client_list_emit_event(EVT_CLIENT_DESTROYED, c);

  /* Destroy the client - this removes from list and unregisters from hub */
  client_destroy(c);

  client_list_request_tile(m);

  LOG_DEBUG("Client destroyed: window=%u", e->window);
}

/*
 * The event mask a managed window has to select.
 *
 * Pointer enter and leave are how the focus component learns which client the
 * pointer is over. Without that selection X walks up to the root, which does
 * select them, and the event then carries the root window as `event`, so the
 * focus handler cannot resolve a client from it.
 *
 * Property change is how the fullscreen component learns that a client, or
 * another program acting for it, changed _NET_WM_STATE, and how the client list
 * learns that a client rewrote its WM_NORMAL_HINTS: a window sends no
 * PropertyNotify unless someone selected it, and an event mask belongs to one
 * window, so there is no other place to ask for it.
 */
uint32_t
client_list_managed_event_mask(void)
{
  return XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_PROPERTY_CHANGE;
}

/*
 * Put a window the WM has taken over into the state every managed window is in:
 * the border width the layout subtracts, the event mask the focus and
 * fullscreen components read, and WM_STATE, which tells other clients the WM
 * manages the window.
 *
 * Whether the window is on screen is a separate question with its own answer: a
 * managed client is unmapped while the view is on another tag and stays managed
 * all the same.
 *
 * Without an X connection (unit tests) there is nothing to send, so the server
 * calls are skipped and only the model changes.
 */
static void
client_list_prepare_client(Client* c)
{
  /* The border width has to be a 4-byte value: xcb_configure_window() takes the
   * value list as CARD32 and copies one per set mask bit, so a uint16_t here
   * makes it read the two stack bytes next to the variable. */
  uint32_t border = CLIENT_LIST_BORDER_WIDTH;
  client_set_border_width(c, (uint16_t) border);

  if (dpy == NULL)
    return;

  xcb_configure_window(dpy, c->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, &border);

  uint32_t event_mask = client_list_managed_event_mask();
  xcb_change_window_attributes(dpy, c->window, XCB_CW_EVENT_MASK, &event_mask);

  if (!client_set_wm_state(c->window, CLIENT_WM_STATE_NORMAL))
    LOG_WARN("WM_STATE atom missing, window %u stays unmarked in X", c->window);
}

/*
 * Ask whether this client's tags are on screen.
 *
 * The tags a client belongs to and the view they are compared with are the tag
 * manager's to keep, so this asks instead of deciding, and the router answers
 * before the call returns. A tree with no component handling the request leaves
 * the answer as it was initialised, and the window is mapped: this component
 * did that for every window before there was a tag manager, and still does when
 * there is none.
 */
static bool
client_list_belongs_on_screen(Client* c)
{
  bool visible = true;

  hub_send_request_data(REQ_CLIENT_VISIBLE, c->target.id, &visible);

  return visible;
}

/*
 * Put a window on screen when the tags it belongs to are the ones the view is
 * on.
 *
 * A window whose tags are not on the current view is managed but not mapped,
 * and the tag manager maps it when a tag it is on is shown. Mapping it here and
 * letting the next view change hide it puts a window on screen on a tag its own
 * tags say it is not on, and nothing takes it off screen until someone switches
 * the view.
 */
static void
client_list_show_if_on_screen(Client* c)
{
  if (!client_list_belongs_on_screen(c))
    return;

  client_show(c->window);
  client_set_mapped(c, true);
  LOG_DEBUG("Mapped client %u", c->window);
}

/*
 * Handle XCB_MAP_REQUEST event.
 * Manages the window (adds it to the client list), then maps it when its tags
 * are on screen and places it where the window rules say it belongs. The two
 * follow the WM_CLASS and WM_NAME reply, which is read in the loop.
 */
void
client_list_on_map_request(void* event)
{
  xcb_map_request_event_t* e = (xcb_map_request_event_t*) event;

  LOG_DEBUG("MAP_REQUEST: window=%u, parent=%u", e->window, e->parent);

  /* Get or create client for this window */
  Client* c = client_get_by_window(e->window);
  if (c == NULL) {
    /* Window not yet known - create a new client */
    LOG_DEBUG("Window %u not yet managed, creating client", e->window);
    c = client_create(e->window);
    if (c == NULL) {
      LOG_WARN("Failed to create client for map request window %u", e->window);
      return;
    }
  }

  /* Mark as managed. A second MapRequest for a managed window changes nothing:
   * the window is already where the WM put it. */
  if (!client_is_managed(c)) {
    client_set_managed(c, true);
    client_list_prepare_client(c);

    /* Ask for the properties before the window is mapped or placed. Both
     * replies are read one loop iteration later: the size hints are what the
     * layout has to respect, and WM_CLASS with WM_NAME are what the rules
     * match. A window shown before the answers arrive is drawn at a rectangle
     * the WM is about to replace, so the map waits with the placement. */
    client_request_size_hints(c);
    bool identity_on_its_way = client_request_identity(c);

    client_list_emit_event(EVT_CLIENT_MANAGED, c);

    /* The poll finishes the manage of a client whose identity is still on its
     * way: it maps and places it once its rules have been applied. */
    if (identity_on_its_way)
      return;

    client_list_show_if_on_screen(c);
    client_list_place_by_rules(c);
  }

  LOG_DEBUG("Client managed: window=%u", e->window);
}

/*
 * Handle XCB_UNMAP_NOTIFY event.
 *
 * Not every unmap is the client taking its window away. The WM unmaps windows
 * itself to hide the clients of a tag that is no longer shown, and client_hide()
 * records each of those requests on the client. The notify that answers one is
 * spent against the record and the client stays managed: showing the tag again
 * maps it back. An unmap the WM did not ask for is a withdrawal.
 *
 * The record cannot be replaced by reading `mapped`, the flag the visibility
 * code keeps: a batch of tag actions runs both a hide and a later show before
 * the loop reads the notifies, so `mapped` is true for an unmap the WM caused.
 * The count still remembers that one notify is owed, and the show does not
 * touch it.
 */
void
client_list_on_unmap_notify(void* event)
{
  xcb_unmap_notify_event_t* e = (xcb_unmap_notify_event_t*) event;

  LOG_DEBUG("UNMAP_NOTIFY: window=%u, from_configure=%d",
            e->window, e->from_configure);

  /* Get the client for this window */
  Client* c = client_get_by_window(e->window);
  if (c == NULL) {
    LOG_DEBUG("No client for unmapped window %u", e->window);
    return;
  }

  if (!client_is_managed(c)) {
    LOG_DEBUG("Unmapped window %u is not managed, nothing to unmanage", e->window);
    return;
  }

  if (client_take_pending_unmap(c)) {
    LOG_DEBUG("Window %u was unmapped by the WM, client stays managed", e->window);
    return;
  }

  client_set_managed(c, false);
  client_set_mapped(c, false);

  /* ICCCM 4.1.3.1: a window its client withdrew is no longer managed, and
   * other clients read that from WM_STATE, not from our client list. */
  if (!client_set_wm_state(e->window, CLIENT_WM_STATE_WITHDRAWN))
    LOG_WARN("WM_STATE atom missing, withdrawn window %u stays marked", e->window);

  client_list_emit_event(EVT_CLIENT_UNMANAGED, c);
  client_list_request_tile(client_get_monitor(c));

  LOG_DEBUG("Client unmanaged: window=%u", e->window);
}

/*
 * Handle XCB_PROPERTY_NOTIFY for a managed window.
 *
 * A client that changes its minimum or maximum size while running writes
 * WM_NORMAL_HINTS again, and that is the only notice the WM gets of it. The
 * read is deferred: a handler may not wait on a reply, so this sends the
 * request and client_list_poll_properties() lays the window out again when the
 * answer arrives and the constraints are not the ones it was tiled to.
 */
void
client_list_on_property_notify(void* event)
{
  xcb_property_notify_event_t* e = (xcb_property_notify_event_t*) event;

  if (!client_is_normal_hints_atom(e->atom))
    return;

  /* A window the WM does not manage is not laid out by it, so reading the
   * hints would cost a round trip nobody acts on. */
  Client* c = client_get_by_window(e->window);
  if (c == NULL || !client_is_managed(c))
    return;

  LOG_DEBUG("WM_NORMAL_HINTS changed on window=%u, re-reading", e->window);
  client_request_size_hints(c);
}

/*
 * Emit a client lifecycle event to subscribers.
 */
void
client_list_emit_event(enum ClientListEventType type, Client* c)
{
  hub_emit((EventType) type, (TargetID) c->window, NULL);
}

/*
 * Get the number of currently managed clients.
 */
uint32_t
client_list_managed_count(void)
{
  return client_count_managed();
}

/*
 * Check if a window is managed.
 */
bool
client_list_is_managed(xcb_window_t window)
{
  Client* c = client_get_by_window(window);
  return c != NULL && client_is_managed(c);
}

/*
 * Take the replies that have arrived for the properties a managed window
 * carries: its size hints, and the WM_CLASS and WM_NAME a window rule matches.
 *
 * The size hints re-tile the monitor they change. The identity read is the
 * manage path's, which held a window's placement until its own names were
 * known, so the client is placed here instead.
 */
void
client_list_poll_properties(void)
{
  Client* c = client_list_sentinel()->next;

  while (c != client_list_sentinel()) {
    /* Snapshot the link: re-tiling does not remove clients today, but a
     * request that does would leave this loop on a freed client. */
    Client* next = c->next;

    /* The identity read is taken first: the manage path held this client's
     * map and placement for it. A layout run for its size hints before that
     * decision would give the window a slot a float rule then takes it back out
     * of, with the slot's rectangle saved as the one to float at. */
    if (client_poll_identity(c) && client_is_managed(c)) {
      client_list_show_if_on_screen(c);
      client_list_place_by_rules(c);
    }

    /* A client still waiting for its own names has no placement to correct:
     * the layout that respects these hints is the one the decision above asks
     * for, and it reads them from the client. */
    bool hints_changed = client_poll_size_hints(c);

    if (hints_changed && !client_identity_pending(c) && client_is_managed(c))
      client_list_request_tile(client_get_monitor(c));

    c = next;
  }
}
