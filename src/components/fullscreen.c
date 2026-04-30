/*
 * Fullscreen Component Implementation
 *
 * Handles fullscreen state for X11 clients using the state machine pattern.
 * Manages the fullscreen state machine and X property for _NET_WM_STATE.
 *
 * The component provides:
 * - FullscreenSM template: WINDOWED ↔ FULLSCREEN
 * - Executor that handles REQ_CLIENT_FULLSCREEN requests via X
 * - Listener for PROPERTY_NOTIFY to sync external _NET_WM_STATE changes
 * - Guards and actions registered with SM registry
 */

#include <stdlib.h>
#include <string.h>

#include "fullscreen.h"
#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-xcb-ewmh.h"
#include "wm-xcb.h"

/*
 * Global component instance
 */
static FullscreenComponent fullscreen_component = {
  .base = {
           .name       = FULLSCREEN_COMPONENT_NAME,
           .requests   = (RequestType[]) { REQ_CLIENT_FULLSCREEN, 0 },
           .targets    = (TargetType[]) { TARGET_TYPE_CLIENT, TARGET_TYPE_NONE },
           .executor   = NULL,
           .registered = false,
           },
  .initialized = false,
};

/*
 * Track if static initialization has occurred
 */
static bool static_init_done = false;

static void
do_static_init(void)
{
  if (static_init_done)
    return;
  static_init_done                     = true;
  fullscreen_component.initialized     = false;
  fullscreen_component.base.registered = false;
}

__attribute__((constructor)) static void
ensure_static_init(void)
{
  do_static_init();
}

/*
 * Internal helper to reset component state
 */
static void
fullscreen_component_reset(void)
{
  fullscreen_component.initialized = false;
}

/*
 * Get the fullscreen component instance
 */
FullscreenComponent*
fullscreen_component_get(void)
{
  return &fullscreen_component;
}

/*
 * Check if component is initialized
 */
bool
fullscreen_component_is_initialized(void)
{
  return fullscreen_component.initialized;
}

/*
 * SM Event Emitter
 *
 * This function is passed to sm_create() to handle event emission.
 * It emits events with the correct target (client window ID).
 */
static void
fullscreen_sm_emit(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) sm;
  (void) from_state;

  Client* c = (Client*) userdata;
  if (c == NULL)
    return;

  EventType event = (to_state == FULLSCREEN_STATE_FULLSCREEN)
                        ? EVT_FULLSCREEN_ENTERED
                        : EVT_FULLSCREEN_EXITED;

  hub_emit(event, c->window, NULL);
}

/*
 * Guard functions
 */

/*
 * Check if a client can enter fullscreen mode.
 * Always allows fullscreen for managed clients.
 */
bool
fullscreen_guard_can_fullscreen(StateMachine* sm, void* data)
{
  (void) data;

  if (sm == NULL || sm->owner == NULL)
    return false;

  Client* c = (Client*) sm->owner;
  return c->managed;
}

/*
 * Check if a client can exit fullscreen mode.
 * Always allows unfullscreen.
 */
bool
fullscreen_guard_can_unfullscreen(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

/*
 * Action functions
 */

/*
 * Action for entering fullscreen.
 * Note: X operations are handled in the executor, not here.
 * This is for SM-side effects if needed.
 */
bool
fullscreen_action_enter_fullscreen(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  /* X operations handled by executor */
  return true;
}

/*
 * Action for exiting fullscreen.
 * Note: X operations are handled in the executor, not here.
 */
bool
fullscreen_action_exit_fullscreen(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  /* X operations handled by executor */
  return true;
}

/*
 * State Machine Template
 */
SMTemplate*
fullscreen_sm_template_create(void)
{
  /* Define states */
  static uint32_t states[] = {
    FULLSCREEN_STATE_WINDOWED,
    FULLSCREEN_STATE_FULLSCREEN,
  };

  /* Define transitions:
   * WINDOWED → FULLSCREEN (guard + action + event)
   * FULLSCREEN → WINDOWED (guard + action + event)
   */
  static SMTransition transitions[] = {
    {
     .from_state = FULLSCREEN_STATE_WINDOWED,
     .to_state   = FULLSCREEN_STATE_FULLSCREEN,
     .guard_fn   = "fs_guard_can_fullscreen",
     .action_fn  = "fs_action_enter_fullscreen",
     .emit_event = EVT_FULLSCREEN_ENTERED,
     },
    {
     .from_state = FULLSCREEN_STATE_FULLSCREEN,
     .to_state   = FULLSCREEN_STATE_WINDOWED,
     .guard_fn   = "fs_guard_can_unfullscreen",
     .action_fn  = "fs_action_exit_fullscreen",
     .emit_event = EVT_FULLSCREEN_EXITED,
     },
  };

  SMTemplate* tmpl = sm_template_create(
      "fullscreen",
      states,
      2,
      transitions,
      2,
      FULLSCREEN_STATE_WINDOWED);

  if (tmpl == NULL) {
    LOG_ERROR("Failed to create fullscreen state machine template");
    return NULL;
  }

  return tmpl;
}

/*
 * State Machine Accessors
 */

/*
 * Get or create the fullscreen state machine for a client.
 */
StateMachine*
fullscreen_get_sm(Client* c)
{
  if (c == NULL)
    return NULL;

  StateMachine* sm = client_get_sm(c, FULLSCREEN_COMPONENT_NAME);
  if (sm != NULL)
    return sm;

  /* Create SM on demand */
  SMTemplate* tmpl = fullscreen_sm_template_create();
  if (tmpl == NULL) {
    LOG_ERROR("Failed to create fullscreen SM template for client");
    return NULL;
  }

  sm = sm_create(c, tmpl, fullscreen_sm_emit, c);
  if (sm == NULL) {
    LOG_ERROR("Failed to create fullscreen SM instance for client");
    return NULL;
  }

  /* Store in client */
  if (!client_set_sm(c, FULLSCREEN_COMPONENT_NAME, sm)) {
    LOG_ERROR("Failed to store fullscreen SM for client");
    sm_destroy(sm);
    return NULL;
  }

  LOG_DEBUG("Created fullscreen SM for client window=%u", c->window);
  return sm;
}

/*
 * Check if a client is in fullscreen state
 */
bool
fullscreen_is_fullscreen(Client* c)
{
  if (c == NULL)
    return false;

  StateMachine* sm = client_get_sm(c, FULLSCREEN_COMPONENT_NAME);
  if (sm == NULL)
    return false;

  return sm_get_state(sm) == FULLSCREEN_STATE_FULLSCREEN;
}

/*
 * Get current fullscreen state for a client
 */
FullscreenState
fullscreen_get_state(Client* c)
{
  if (c == NULL)
    return FULLSCREEN_STATE_WINDOWED;

  StateMachine* sm = client_get_sm(c, FULLSCREEN_COMPONENT_NAME);
  if (sm == NULL)
    return FULLSCREEN_STATE_WINDOWED;

  return (FullscreenState) sm_get_state(sm);
}

/*
 * Set fullscreen state directly (for raw_write from listeners).
 * Does not run guards - use for authoritative external state changes.
 */
void
fullscreen_set_state(Client* c, FullscreenState state)
{
  if (c == NULL)
    return;

  StateMachine* sm = fullscreen_get_sm(c);
  if (sm == NULL) {
    LOG_WARN("Cannot set fullscreen state: failed to get/create SM");
    return;
  }

  sm_raw_write(sm, state);
}

/*
 * XCB Property Update
 */

/*
 * Update the _NET_WM_STATE X property to reflect fullscreen state.
 */
void
fullscreen_update_x_property(Client* c, bool fullscreen)
{
  if (c == NULL || ewmh == NULL)
    return;

  LOG_DEBUG("Updating _NET_WM_STATE for client window=%u fullscreen=%d",
            c->window, fullscreen);

  /* Use EWMH to set/unset _NET_WM_STATE_FULLSCREEN */
  if (fullscreen) {
    /* Add fullscreen atom */
    xcb_ewmh_request_change_wm_state(
        ewmh,
        0, /* screen number */
        c->window,
        XCB_EWMH_WM_STATE_ADD,
        ewmh->_NET_WM_STATE_FULLSCREEN,
        XCB_ATOM_NONE,
        0);
  } else {
    /* Remove fullscreen atom - use REMOVE instead of TOGGLE to avoid sync issues */
    xcb_ewmh_request_change_wm_state(
        ewmh,
        0, /* screen number */
        c->window,
        XCB_EWMH_WM_STATE_REMOVE,
        ewmh->_NET_WM_STATE_FULLSCREEN,
        XCB_ATOM_NONE,
        0);
  }
}

/*
 * Executor: Handle REQ_CLIENT_FULLSCREEN request
 */
static void
fullscreen_executor(struct HubRequest* req)
{
  if (req == NULL)
    return;

  LOG_DEBUG("fullscreen_executor: type=%u target=%" PRIu64,
            req->type, req->target);

  /* Get client from target ID */
  Client* c = (Client*) hub_get_target_by_id(req->target);
  if (c == NULL || c->target.type != TARGET_TYPE_CLIENT) {
    LOG_DEBUG("fullscreen_executor: no client found for target");
    hub_emit(EVT_FULLSCREEN_FAILED, req->target, NULL);
    return;
  }

  /* Get or create SM */
  StateMachine* sm = fullscreen_get_sm(c);
  if (sm == NULL) {
    LOG_ERROR("fullscreen_executor: failed to get fullscreen SM");
    hub_emit(EVT_FULLSCREEN_FAILED, req->target, NULL);
    return;
  }

  /* Determine target state based on current state */
  uint32_t current = sm_get_state(sm);
  uint32_t target  = (current == FULLSCREEN_STATE_FULLSCREEN)
                         ? FULLSCREEN_STATE_WINDOWED
                         : FULLSCREEN_STATE_FULLSCREEN;

  /* Perform X operation */
  fullscreen_update_x_property(c, target == FULLSCREEN_STATE_FULLSCREEN);

  /* Update SM state (raw_write - X operation succeeded) */
  sm_raw_write(sm, target);

  LOG_DEBUG("fullscreen_executor: toggled fullscreen for client window=%u to state=%u",
            c->window, target);
}

/*
 * PropertyNotify handler for _NET_WM_STATE monitoring
 */
void
fullscreen_on_property_notify(void* event)
{
  xcb_property_notify_event_t* e = (xcb_property_notify_event_t*) event;

  LOG_DEBUG("PROPERTY_NOTIFY: window=%u atom=%u state=%d",
            e->window, e->atom, e->state);

  /* Check if this is _NET_WM_STATE property */
  if (ewmh == NULL || e->atom != ewmh->_NET_WM_STATE)
    return;

  /* Get client for this window */
  Client* c = client_get_by_window(e->window);
  if (c == NULL)
    return;

  /* Check if _NET_WM_STATE contains _NET_WM_STATE_FULLSCREEN */
  bool is_fullscreen = ewmh_check_wm_state_fullscreen(ewmh, c->window);

  /* Get or create the SM (on-demand) */
  StateMachine* sm = fullscreen_get_sm(c);
  if (sm == NULL) {
    LOG_WARN("fullscreen listener: failed to get SM for client window=%u", c->window);
    return;
  }

  /* Get current SM state */
  FullscreenState current = (FullscreenState) sm_get_state(sm);

  /* Sync if different - external change happened */
  if (is_fullscreen && current == FULLSCREEN_STATE_WINDOWED) {
    LOG_DEBUG("fullscreen listener: external fullscreen change detected, syncing SM");
    sm_raw_write(sm, FULLSCREEN_STATE_FULLSCREEN);
  } else if (!is_fullscreen && current == FULLSCREEN_STATE_FULLSCREEN) {
    LOG_DEBUG("fullscreen listener: external unfullscreen change detected, syncing SM");
    sm_raw_write(sm, FULLSCREEN_STATE_WINDOWED);
  }
}

/*
 * Component initialization
 */
bool
fullscreen_component_init(void)
{
  /* Check if already registered in hub */
  HubComponent* existing = hub_get_component_by_name(FULLSCREEN_COMPONENT_NAME);
  if (existing != NULL) {
    if (fullscreen_component.initialized) {
      LOG_DEBUG("Fullscreen component already initialized and registered");
      return true;
    }
    LOG_DEBUG("Component registered with hub, completing initialization");
    fullscreen_component.initialized = true;
    return true;
  }

  /* Reset if inconsistent state */
  if (fullscreen_component.initialized) {
    LOG_DEBUG("Fullscreen component in inconsistent state, resetting");
    fullscreen_component_reset();
  }

  LOG_DEBUG("Initializing fullscreen component");

  /* Register guards and actions with SM registry */
  sm_register_guard("fs_guard_can_fullscreen", fullscreen_guard_can_fullscreen);
  sm_register_guard("fs_guard_can_unfullscreen", fullscreen_guard_can_unfullscreen);
  sm_register_action("fs_action_enter_fullscreen", fullscreen_action_enter_fullscreen);
  sm_register_action("fs_action_exit_fullscreen", fullscreen_action_exit_fullscreen);

  /* Register executor */
  fullscreen_component.base.executor = fullscreen_executor;

  /* Register with hub */
  hub_register_component(&fullscreen_component.base);

  /* Register XCB handler for PROPERTY_NOTIFY */
  int result = xcb_handler_register(
      XCB_PROPERTY_NOTIFY,
      &fullscreen_component.base,
      fullscreen_on_property_notify);

  if (result != 0) {
    LOG_ERROR("Failed to register PROPERTY_NOTIFY handler for fullscreen component");
    /* Unregister from hub since we're not fully initialized */
    hub_unregister_component(FULLSCREEN_COMPONENT_NAME);
    fullscreen_component_reset();
    return false;
  }

  fullscreen_component.initialized = true;
  LOG_DEBUG("Fullscreen component initialized successfully");

  return true;
}

/*
 * Component shutdown
 */
void
fullscreen_component_shutdown(void)
{
  if (!fullscreen_component.initialized) {
    return;
  }

  LOG_DEBUG("Shutting down fullscreen component");

  /* Unregister XCB handlers */
  xcb_handler_unregister_component(&fullscreen_component.base);

  /* Unregister from hub */
  hub_unregister_component(FULLSCREEN_COMPONENT_NAME);

  /* Unregister guards and actions */
  sm_unregister_guard("fs_guard_can_fullscreen");
  sm_unregister_guard("fs_guard_can_unfullscreen");
  sm_unregister_action("fs_action_enter_fullscreen");
  sm_unregister_action("fs_action_exit_fullscreen");

  fullscreen_component.initialized = false;
  LOG_DEBUG("Fullscreen component shutdown complete");
}
