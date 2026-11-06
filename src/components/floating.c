/*
 * Floating Component Implementation
 *
 * Moves a client between the tiled layout and a free position, and keeps the
 * slot it left so it can be given back.
 *
 * The component provides:
 * - FloatingSM template: TILED ↔ FLOATING
 * - Executor that handles REQ_CLIENT_FLOAT and REQ_CLIENT_TILE by transitioning
 *   the client's machine
 * - Guards and actions registered with the SM registry
 *
 * Nothing here writes a window's position except the exit action, which puts
 * back the geometry the client had: while a client is floating the layout does
 * not place it, and while it is tiled the layout is what decides where it goes.
 */

#include <stdlib.h>

#include "floating.h"
#include "src/actions/action-registry.h"
#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"

/*
 * Cached FloatingSM template - created once, reused for all clients.
 * sm_create() stores the pointer instead of copying, so a template per client
 * would leak one per window.
 */
static SMTemplate* cached_floating_template = NULL;

/*
 * Global component instance
 */
static FloatingComponent floating_component = {
  .base = {
           .name                  = FLOATING_COMPONENT_NAME,
           .requests              = (RequestType[]) { REQ_CLIENT_FLOAT, REQ_CLIENT_TILE, 0 },
           .accepted_target_names = (const char*[]) { "client", NULL },
           .accepted_targets      = NULL,
           .executor              = NULL,
           .registered            = false,
           },
  .initialized = false,
};

static bool static_init_done = false;

static void
do_static_init(void)
{
  if (static_init_done)
    return;
  static_init_done                   = true;
  floating_component.initialized     = false;
  floating_component.base.registered = false;
}

__attribute__((constructor)) static void
ensure_static_init(void)
{
  do_static_init();
}

static void
floating_component_reset(void)
{
  floating_component.initialized = false;
}

FloatingComponent*
floating_component_get(void)
{
  return &floating_component;
}

bool
floating_component_is_initialized(void)
{
  return floating_component.initialized;
}

/*
 * SM Event Emitter
 *
 * The target is the client window, which is what the tiling component needs to
 * find the monitor to arrange again.
 */
static void
floating_sm_emit(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) sm;
  (void) from_state;

  Client* c = (Client*) userdata;
  if (c == NULL)
    return;

  EventType event = (to_state == FLOATING_STATE_FLOATING)
                        ? EVT_CLIENT_FLOATED
                        : EVT_CLIENT_TILED;

  hub_emit(event, c->window, NULL);
}

/*
 * Guard functions
 */

/*
 * A window the WM does not manage has no slot to give back, so it is refused
 * rather than left in a state nothing can undo.
 */
bool
floating_guard_can_float(StateMachine* sm, void* data)
{
  (void) data;

  if (sm == NULL || sm_get_owner(sm) == NULL)
    return false;

  Client* c = (Client*) sm_get_owner(sm);
  return client_is_managed(c);
}

bool
floating_guard_can_tile(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

/*
 * Action functions
 */

/*
 * Remember the rectangle the layout gave the client.
 *
 * The saved copy is the outside geometry the client asks for, which is what the
 * floating client keeps while it is out of the layout: a tag switch hides and
 * shows it again without touching this, so the size survives the round trip.
 */
bool
floating_action_enter_floating(StateMachine* sm, void* data)
{
  (void) data;

  if (sm == NULL || sm_get_owner(sm) == NULL)
    return false;

  FloatingSavedGeometry* saved = (FloatingSavedGeometry*) sm_get_data(sm);
  if (saved == NULL) {
    LOG_ERROR("floating: the client's machine has no place to keep its geometry");
    return false;
  }

  Client* c = (Client*) sm_get_owner(sm);

  saved->x      = c->x;
  saved->y      = c->y;
  saved->width  = c->width;
  saved->height = c->height;

  LOG_DEBUG("floating: saved (%d,%d %ux%u) for window=%u",
            saved->x, saved->y, saved->width, saved->height, c->window);

  return true;
}

/*
 * Put the client back where it was.
 *
 * The layout that the emitted event asks for decides the slot it ends in, and
 * this is what a client gets back when no layout runs: no monitor, or nothing
 * left to tile beside it. Configuring here also means the window is in its slot
 * before the re-tile reaches it, instead of after.
 */
bool
floating_action_exit_floating(StateMachine* sm, void* data)
{
  (void) data;

  if (sm == NULL || sm_get_owner(sm) == NULL)
    return false;

  FloatingSavedGeometry* saved = (FloatingSavedGeometry*) sm_get_data(sm);
  if (saved == NULL) {
    LOG_ERROR("floating: the client's machine has no saved geometry to restore");
    return false;
  }

  Client* c = (Client*) sm_get_owner(sm);

  c->x      = saved->x;
  c->y      = saved->y;
  c->width  = saved->width;
  c->height = saved->height;

  client_configure_from_struct(c);

  LOG_DEBUG("floating: restored (%d,%d %ux%u) for window=%u",
            c->x, c->y, c->width, c->height, c->window);

  return true;
}

/*
 * State Machine Template
 */
SMTemplate*
floating_sm_template_create(void)
{
  static uint32_t states[] = {
    FLOATING_STATE_TILED,
    FLOATING_STATE_FLOATING,
  };

  static SMTransition transitions[] = {
    {
     .from_state = FLOATING_STATE_TILED,
     .to_state   = FLOATING_STATE_FLOATING,
     .guard_fn   = "floating_guard_can_float",
     .action_fn  = "floating_action_enter_floating",
     .emit_event = EVT_CLIENT_FLOATED,
     },
    {
     .from_state = FLOATING_STATE_FLOATING,
     .to_state   = FLOATING_STATE_TILED,
     .guard_fn   = "floating_guard_can_tile",
     .action_fn  = "floating_action_exit_floating",
     .emit_event = EVT_CLIENT_TILED,
     },
  };

  SMTemplate* tmpl = sm_template_create(
      FLOATING_COMPONENT_NAME,
      states,
      2,
      transitions,
      2,
      FLOATING_STATE_TILED);

  if (tmpl == NULL) {
    LOG_ERROR("Failed to create floating state machine template");
    return NULL;
  }

  return tmpl;
}

/*
 * State Machine Accessors
 */

/*
 * Get or create the floating state machine for a client.
 *
 * The machine owns the saved geometry, so a client that goes away takes it
 * with it and the component does not have to be running to free it.
 */
StateMachine*
floating_get_sm(Client* c)
{
  if (c == NULL)
    return NULL;

  StateMachine* sm = client_get_sm(c, FLOATING_COMPONENT_NAME);
  if (sm != NULL)
    return sm;

  if (cached_floating_template == NULL) {
    cached_floating_template = floating_sm_template_create();
    if (cached_floating_template == NULL) {
      LOG_ERROR("Failed to create floating SM template for client");
      return NULL;
    }
  }

  FloatingSavedGeometry* saved = calloc(1, sizeof *saved);
  if (saved == NULL) {
    LOG_ERROR("Failed to allocate the saved geometry for client window=%u", c->window);
    return NULL;
  }

  sm = sm_create(c, cached_floating_template, floating_sm_emit, c);
  if (sm == NULL) {
    LOG_ERROR("Failed to create floating SM instance for client");
    free(saved);
    return NULL;
  }

  sm_set_owned_data(sm, saved, free);

  if (!client_set_sm(c, FLOATING_COMPONENT_NAME, sm)) {
    LOG_ERROR("Failed to store floating SM for client");
    sm_destroy(sm);
    return NULL;
  }

  LOG_DEBUG("Created floating SM for client window=%u", c->window);
  return sm;
}

bool
floating_is_floating(Client* c)
{
  return floating_get_state(c) == FLOATING_STATE_FLOATING;
}

FloatingState
floating_get_state(Client* c)
{
  if (c == NULL)
    return FLOATING_STATE_TILED;

  StateMachine* sm = client_get_sm(c, FLOATING_COMPONENT_NAME);
  if (sm == NULL)
    return FLOATING_STATE_TILED;

  return (FloatingState) sm_get_state(sm);
}

const FloatingSavedGeometry*
floating_saved_geometry(Client* c)
{
  if (c == NULL)
    return NULL;

  StateMachine* sm = client_get_sm(c, FLOATING_COMPONENT_NAME);
  if (sm == NULL)
    return NULL;

  return (const FloatingSavedGeometry*) sm_get_data(sm);
}

/*
 * Executor: handle REQ_CLIENT_FLOAT and REQ_CLIENT_TILE
 *
 * A request is intent, so it goes through the guard and the action the template
 * declares. A refusal is reported and leaves the window where it is.
 */
static void
floating_executor(struct HubRequest* req)
{
  if (req == NULL)
    return;

  LOG_DEBUG("floating_executor: type=%u target=%" PRIu64, req->type, req->target);

  Client* c = (Client*) hub_get_target_by_id(req->target);
  if (c == NULL || c->target.type_id != hub_get_target_type_id_by_name("client")) {
    LOG_DEBUG("floating_executor: no client found for target");
    hub_emit(EVT_FLOATING_FAILED, req->target, NULL);
    return;
  }

  StateMachine* sm = floating_get_sm(c);
  if (sm == NULL) {
    LOG_ERROR("floating_executor: failed to get floating SM");
    hub_emit(EVT_FLOATING_FAILED, req->target, NULL);
    return;
  }

  uint32_t target = (req->type == REQ_CLIENT_FLOAT) ? FLOATING_STATE_FLOATING
                                                    : FLOATING_STATE_TILED;

  if (!sm_transition(sm, target)) {
    LOG_DEBUG("floating_executor: transition to state=%u refused for window=%u",
              target, c->window);
    hub_emit(EVT_FLOATING_FAILED, req->target, NULL);
    return;
  }

  LOG_DEBUG("floating_executor: window=%u is now state=%u", c->window, target);
}

/*
 * Action: float the client the invocation resolved, or tile it again.
 *
 * The direction is decided here so a keybinding sends the request that names
 * what it wants; the work itself is the executor's, so both requests and the
 * action end in the same code.
 */
static bool
floating_action_toggle(ActionInvocation* inv)
{
  if (inv == NULL || inv->target == TARGET_ID_NONE)
    return false;

  Client* c = (Client*) hub_get_target_by_id(inv->target);
  if (c == NULL)
    return false;

  hub_send_request(floating_is_floating(c) ? REQ_CLIENT_TILE : REQ_CLIENT_FLOAT,
                   inv->target);
  return true;
}

static Action floating_action = {
  .name            = "floating.toggle",
  .description     = "Float the focused client, or tile it again",
  .callback        = floating_action_toggle,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_CLIENT,
  .target_required = true,
  .userdata        = NULL,
};

/*
 * Component initialization
 */
bool
floating_component_init(void)
{
  HubComponent* existing = hub_get_component_by_name(FLOATING_COMPONENT_NAME);
  if (existing != NULL) {
    if (floating_component.initialized) {
      LOG_DEBUG("Floating component already initialized and registered");
      return true;
    }
    LOG_DEBUG("Component registered with hub, completing initialization");
    floating_component.initialized = true;
    return true;
  }

  if (floating_component.initialized) {
    LOG_DEBUG("Floating component in inconsistent state, resetting");
    floating_component_reset();
  }

  LOG_DEBUG("Initializing floating component");

  sm_register_guard("floating_guard_can_float", floating_guard_can_float);
  sm_register_guard("floating_guard_can_tile", floating_guard_can_tile);
  sm_register_action("floating_action_enter_floating", floating_action_enter_floating);
  sm_register_action("floating_action_exit_floating", floating_action_exit_floating);

  floating_component.base.executor = floating_executor;

  /* The action goes in before the component becomes visible to the hub */
  if (!action_register(&floating_action)) {
    LOG_ERROR("Failed to register action '%s'", floating_action.name);
    sm_unregister_guard("floating_guard_can_float");
    sm_unregister_guard("floating_guard_can_tile");
    sm_unregister_action("floating_action_enter_floating");
    sm_unregister_action("floating_action_exit_floating");
    return false;
  }

  hub_register_component(&floating_component.base);

  if (hub_get_component_by_name(FLOATING_COMPONENT_NAME) == NULL) {
    LOG_ERROR("Floating component registration was refused");
    action_unregister(floating_action.name);
    sm_unregister_guard("floating_guard_can_float");
    sm_unregister_guard("floating_guard_can_tile");
    sm_unregister_action("floating_action_enter_floating");
    sm_unregister_action("floating_action_exit_floating");
    floating_component_reset();
    return false;
  }

  cached_floating_template = floating_sm_template_create();
  if (cached_floating_template == NULL) {
    hub_unregister_component(FLOATING_COMPONENT_NAME);
    action_unregister(floating_action.name);
    sm_unregister_guard("floating_guard_can_float");
    sm_unregister_guard("floating_guard_can_tile");
    sm_unregister_action("floating_action_enter_floating");
    sm_unregister_action("floating_action_exit_floating");
    floating_component_reset();
    return false;
  }

  floating_component.initialized = true;
  LOG_DEBUG("Floating component initialized successfully");
  return true;
}

void
floating_component_shutdown(void)
{
  if (!floating_component.initialized) {
    return;
  }

  LOG_DEBUG("Shutting down floating component");

  hub_unregister_component(FLOATING_COMPONENT_NAME);

  action_unregister(floating_action.name);

  sm_unregister_guard("floating_guard_can_float");
  sm_unregister_guard("floating_guard_can_tile");
  sm_unregister_action("floating_action_enter_floating");
  sm_unregister_action("floating_action_exit_floating");

  /* Clients hold machines that point at this template, so the component goes
   * after the client list. */
  if (cached_floating_template != NULL) {
    sm_template_destroy(cached_floating_template);
    cached_floating_template = NULL;
  }

  floating_component.initialized = false;
  LOG_DEBUG("Floating component shutdown complete");
}
