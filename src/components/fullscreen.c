/*
 * Fullscreen Component Implementation
 *
 * Handles fullscreen state for X11 clients using the state machine pattern.
 * Manages the fullscreen state machine and X property for _NET_WM_STATE.
 *
 * The component provides:
 * - FullscreenSM template: WINDOWED ↔ FULLSCREEN
 * - Executor that handles REQ_CLIENT_FULLSCREEN by transitioning the client's
 *   machine, whose action issues the X property change
 * - Listener for PROPERTY_NOTIFY to sync external _NET_WM_STATE changes
 * - Guards and actions registered with SM registry
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcbext.h>

#include "fullscreen.h"
#include "src/actions/action-registry.h"
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
 * Cached FullscreenSM template - created once, reused for all clients.
 * sm_create() stores the pointer instead of copying, so a template per client
 * would leak one per window.
 */
static SMTemplate* cached_fullscreen_template = NULL;

/*
 * Global component instance
 */
static FullscreenComponent fullscreen_component = {
  .base = {
           .name                  = FULLSCREEN_COMPONENT_NAME,
           .requests              = (RequestType[]) { REQ_CLIENT_FULLSCREEN, 0 },
           .accepted_target_names = (const char*[]) { "client", NULL },
           .accepted_targets      = NULL,
           .executor              = NULL,
           .registered            = false,
           },
  .initialized = false,
};

/*
 * Track if static initialization has occurred
 */
static bool static_init_done = false;

/*
 * _NET_WM_STATE reads that are in flight.
 *
 * A PropertyNotify says the list changed, not what it now holds, so every one
 * of them costs a query. The answer is a reply, not an event, so the cookie is
 * kept here and read from the loop's poll callback: reading it in the handler
 * would stop the WM until the server answered.
 *
 * One entry per notification, not one per window: two changes in a row are two
 * reads, and the later answer must not be applied before the earlier one.
 */
typedef struct FullscreenStateRead {
  xcb_get_property_cookie_t cookie;
  xcb_window_t              window;
} FullscreenStateRead;

static FullscreenStateRead* state_reads        = NULL;
static size_t               state_read_count   = 0;
static size_t               state_read_cap     = 0;
static bool                 state_read_polling = false;

static void fullscreen_state_read_shutdown(void);

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
 * The X half of both rows. Enter and exit differ only in which way the property
 * goes, so the shared part is here.
 */
static bool
fullscreen_action_write_state(StateMachine* sm, bool fullscreen)
{
  if (sm->owner == NULL)
    return false;

  fullscreen_update_x_property((Client*) sm->owner, fullscreen);
  return true;
}

/*
 * Action for entering fullscreen: the transition's own X side effect.
 *
 * The write lives here rather than in the executor so that every path into the
 * machine - a keybinding request today, a direct transition tomorrow - asks the
 * server for the same thing, and a refused transition asks for nothing. The
 * request is ADD rather than TOGGLE so that running it twice leaves one
 * _NET_WM_STATE_FULLSCREEN in the list.
 */
bool
fullscreen_action_enter_fullscreen(StateMachine* sm, void* data)
{
  (void) data;
  return fullscreen_action_write_state(sm, true);
}

/*
 * Action for exiting fullscreen: removes the atom the enter action added.
 */
bool
fullscreen_action_exit_fullscreen(StateMachine* sm, void* data)
{
  (void) data;
  return fullscreen_action_write_state(sm, false);
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
   * WINDOWED → FULLSCREEN (guard + action writes _NET_WM_STATE + event)
   * FULLSCREEN → WINDOWED (guard + action removes _NET_WM_STATE + event)
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
  SMTemplate* tmpl = cached_fullscreen_template;
  if (tmpl == NULL) {
    tmpl = fullscreen_sm_template_create();
    if (tmpl == NULL) {
      LOG_ERROR("Failed to create fullscreen SM template for client");
      return NULL;
    }
    cached_fullscreen_template = tmpl;
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
  if (c == NULL || c->target.type_id != hub_get_target_type_id_by_name("client")) {
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

  /* A keybinding asks for a state, so it goes through the guard and the action
     the template declares. Refused means nothing was written to the window. */
  if (!sm_transition(sm, target)) {
    LOG_DEBUG("fullscreen_executor: transition to state=%u refused for window=%u",
              target, c->window);
    hub_emit(EVT_FULLSCREEN_FAILED, req->target, NULL);
    return;
  }

  LOG_DEBUG("fullscreen_executor: toggled fullscreen for client window=%u to state=%u",
            c->window, target);
}

/*
 * Action: toggle fullscreen on the client the invocation resolved.
 * The work happens in this component's executor, so the keybinding path and
 * the request path are the same code.
 */
static bool
fullscreen_action_toggle(ActionInvocation* inv)
{
  if (inv == NULL || inv->target == TARGET_ID_NONE)
    return false;

  hub_send_request(REQ_CLIENT_FULLSCREEN, inv->target);
  return true;
}

static Action fullscreen_action = {
  .name            = "fullscreen.toggle",
  .description     = "Toggle fullscreen on the focused client",
  .callback        = fullscreen_action_toggle,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_CLIENT,
  .target_required = true,
  .userdata        = NULL,
};

/*
 * Apply a _NET_WM_STATE reply: the state machine follows what the server and
 * the client agree on, so an external change lands as a raw write.
 */
static void
fullscreen_sync_from_state_reply(xcb_window_t window, xcb_get_property_reply_t* reply)
{
  Client* c = client_get_by_window(window);
  if (c == NULL)
    return;

  bool is_fullscreen = ewmh_wm_state_is_fullscreen(ewmh, reply);

  /* Get or create the SM (on-demand) */
  StateMachine* sm = fullscreen_get_sm(c);
  if (sm == NULL) {
    LOG_WARN("fullscreen listener: failed to get SM for client window=%u", c->window);
    return;
  }

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
 * Apply the answers that have arrived, in the order the notifications were
 * queued, and keep the rest for the next iteration.
 *
 * A later answer must not be applied before an earlier one. Both are ready by
 * the time this runs when the two notifications arrived in one batch, and
 * applying the newer one first leaves the state machine on the older reply. A
 * reply that has not arrived stops the walk: the ones after it are later.
 */
size_t
fullscreen_state_read_drain(FullscreenStateReadArrived arrived,
                            FullscreenStateReadApply   apply,
                            void*                      userdata)
{
  size_t applied = 0;

  while (state_read_count > 0) {
    FullscreenStateRead head  = state_reads[0];
    void*               reply = NULL;

    if (!arrived(userdata, head.cookie, head.window, &reply))
      break;

    /* Drop this entry before applying. Applying can queue another read, and
       that entry has to stay behind the ones still in flight. */
    state_read_count--;
    memmove(state_reads, state_reads + 1, state_read_count * sizeof *state_reads);

    apply(userdata, head.window, reply);
    applied++;
  }

  return applied;
}

/*
 * The loop's reply source: xcb_poll_for_reply() reports "not yet" instead of
 * waiting, so the loop stays free.
 */
static bool
fullscreen_state_read_arrived_from_x(void*                     userdata,
                                     xcb_get_property_cookie_t cookie,
                                     xcb_window_t              window,
                                     void**                    reply)
{
  (void) userdata;
  (void) window;

  xcb_generic_error_t* error = NULL;
  if (!xcb_poll_for_reply(dpy, cookie.sequence, reply, &error))
    return false;

  free(error);
  return true;
}

static void
fullscreen_state_read_apply_from_x(void* userdata, xcb_window_t window, void* reply)
{
  (void) userdata;

  if (reply != NULL)
    fullscreen_sync_from_state_reply(window, reply);

  free(reply);
}

static void
fullscreen_state_read_poll(void* userdata)
{
  if (dpy == NULL) {
    state_read_count = 0;
  }

  fullscreen_state_read_drain(
      fullscreen_state_read_arrived_from_x, fullscreen_state_read_apply_from_x, NULL);

  if (state_read_count == 0 && state_read_polling) {
    wm_loop_remove_poll_handler(fullscreen_state_read_poll, NULL);
    state_read_polling = false;
  }
}

static bool
fullscreen_state_read_queue_ensure(void)
{
  if (state_read_count < state_read_cap)
    return true;

  size_t               cap   = state_read_cap == 0 ? 4 : state_read_cap * 2;
  FullscreenStateRead* grown = realloc(state_reads, cap * sizeof *grown);
  if (grown == NULL) {
    LOG_ERROR("fullscreen: cannot track the _NET_WM_STATE reply: %s", strerror(errno));
    return false;
  }
  state_reads    = grown;
  state_read_cap = cap;
  return true;
}

static bool
fullscreen_state_read_queue_add(xcb_get_property_cookie_t cookie, xcb_window_t window)
{
  if (!fullscreen_state_read_queue_ensure())
    return false;

  state_reads[state_read_count].cookie = cookie;
  state_reads[state_read_count].window = window;
  state_read_count++;
  return true;
}

#ifdef WM_HUB_TESTING
bool
fullscreen_state_read_queue_push_for_tests(xcb_get_property_cookie_t cookie, xcb_window_t window)
{
  return fullscreen_state_read_queue_add(cookie, window);
}

size_t
fullscreen_state_read_queue_count_for_tests(void)
{
  return state_read_count;
}

void
fullscreen_state_read_queue_reset_for_tests(void)
{
  fullscreen_state_read_shutdown();
}
#endif

/*
 * Ask for a window's _NET_WM_STATE and arrange to read the answer.
 */
static bool
fullscreen_state_read_begin(xcb_window_t window)
{
  if (dpy == NULL || ewmh == NULL)
    return false;

  if (!state_read_polling) {
    if (!wm_loop_add_poll_handler(fullscreen_state_read_poll, NULL)) {
      LOG_ERROR("fullscreen: cannot read _NET_WM_STATE, the loop refused the callback");
      return false;
    }
    state_read_polling = true;
  }

  /* Reserve the slot before the request. A request passed as an argument is
     sent first, and a failed realloc then drops the cookie, so the reply is
     never discarded. */
  if (!fullscreen_state_read_queue_ensure())
    return false;

  return fullscreen_state_read_queue_add(ewmh_request_wm_state(ewmh, window), window);
}

/*
 * Drop the reads still in flight. Their replies are never read, so libxcb is
 * told to throw them away instead of holding them for a caller that is gone.
 */
static void
fullscreen_state_read_shutdown(void)
{
  if (dpy != NULL) {
    for (size_t i = 0; i < state_read_count; i++)
      xcb_discard_reply(dpy, state_reads[i].cookie.sequence);
  }

  free(state_reads);
  state_reads      = NULL;
  state_read_count = 0;
  state_read_cap   = 0;

  if (state_read_polling) {
    wm_loop_remove_poll_handler(fullscreen_state_read_poll, NULL);
    state_read_polling = false;
  }
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

  /* A window the WM does not manage has no state machine to sync, and asking
     for its list would cost a round trip nobody reads. */
  if (client_get_by_window(e->window) == NULL)
    return;

  if (!fullscreen_state_read_begin(e->window))
    LOG_WARN("fullscreen: could not queue a read of window %u's _NET_WM_STATE", e->window);
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

  /* Actions go in before the component becomes visible to the hub */
  if (!action_register(&fullscreen_action)) {
    LOG_ERROR("Failed to register action '%s'", fullscreen_action.name);
    sm_unregister_guard("fs_guard_can_fullscreen");
    sm_unregister_guard("fs_guard_can_unfullscreen");
    sm_unregister_action("fs_action_enter_fullscreen");
    sm_unregister_action("fs_action_exit_fullscreen");
    return false;
  }

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
    sm_unregister_guard("fs_guard_can_fullscreen");
    sm_unregister_guard("fs_guard_can_unfullscreen");
    sm_unregister_action("fs_action_enter_fullscreen");
    sm_unregister_action("fs_action_exit_fullscreen");
    action_unregister(fullscreen_action.name);
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

  /* Unregister guards, SM actions, and the registered action */
  sm_unregister_guard("fs_guard_can_fullscreen");
  sm_unregister_guard("fs_guard_can_unfullscreen");
  sm_unregister_action("fs_action_enter_fullscreen");
  sm_unregister_action("fs_action_exit_fullscreen");
  action_unregister(fullscreen_action.name);

  fullscreen_state_read_shutdown();

  /* Free cached template after the clients that reference it are gone */
  if (cached_fullscreen_template != NULL) {
    sm_template_destroy(cached_fullscreen_template);
    cached_fullscreen_template = NULL;
  }

  fullscreen_component.initialized = false;
  LOG_DEBUG("Fullscreen component shutdown complete");
}
