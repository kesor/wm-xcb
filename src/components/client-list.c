/*
 * Client List Component Implementation
 *
 * Handles XCB events for client lifecycle management.
 * Manages the global client list via client.h, and sends REQ_MONITOR_TILE when
 * a window joins or leaves a monitor so the remaining windows are rearranged.
 */

#include <stdlib.h>

#include "client-list.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "wm-log.h"
#include "wm-xcb.h"

/*
 * Border drawn around a managed window. config.def.h has the same value in
 * default_borders.width, but including it here would copy the whole compiled-in
 * configuration into this file, unused. The tiling component writes the same
 * width from its own default, so a change to the configured border has to be
 * made in both places until one path reads config.def.h for both.
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
  hub_register_component(client_list_component_base());

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
 * Put a window the WM has taken over on screen: give it the border, tell other
 * clients it is managed through WM_STATE, and map it.
 *
 * Geometry and stacking belong to the layout component; X places a newly
 * mapped window on top of its siblings, which is the order tiling wants.
 *
 * Without an X connection (unit tests) there is nothing to send, so the model
 * is updated by the caller and the server calls are skipped.
 */
static void
client_list_map_client(Client* c)
{
  if (dpy == NULL)
    return;

  uint16_t border = CLIENT_LIST_BORDER_WIDTH;
  xcb_configure_window(dpy, c->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, &border);
  client_set_border_width(c, border);

  if (!client_set_wm_state(c->window, CLIENT_WM_STATE_NORMAL))
    LOG_WARN("WM_STATE atom missing, window %u stays unmarked in X", c->window);

  client_show(c->window);
}

/*
 * Handle XCB_MAP_REQUEST event.
 * Manages the window (adds it to the client list) and maps it.
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
    client_list_map_client(c);
    client_set_mapped(c, true);
    client_list_emit_event(EVT_CLIENT_MANAGED, c);
    client_list_request_tile(client_get_monitor(c));
  }

  LOG_DEBUG("Client managed: window=%u", e->window);
}

/*
 * Handle XCB_UNMAP_NOTIFY event.
 * Unmanages the window (removes from client list).
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

  /* Only emit unmanage event if client was managed */
  if (client_is_managed(c)) {
    client_set_managed(c, false);
    client_set_mapped(c, false);
    client_list_emit_event(EVT_CLIENT_UNMANAGED, c);
    client_list_request_tile(client_get_monitor(c));
  }

  LOG_DEBUG("Client unmanaged: window=%u", e->window);
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