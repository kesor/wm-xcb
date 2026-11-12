/*
 * Focus Component Implementation
 *
 * Handles focus state for X11 clients using the state machine pattern.
 * Manages the focus state machine and XCB events for focus tracking.
 *
 * The component provides:
 * - FocusSM template: UNFOCUSED ↔ FOCUSED
 * - XCB listener for ENTER_NOTIFY and LEAVE_NOTIFY
 * - raw_write to SM on mouse enter/leave events
 * - Guards for transition validation
 * - the X input focus: it subscribes to its own FOCUSED and UNFOCUSED events
 *   and follows them with xcb_set_input_focus, gives focus to a client when it
 *   is managed, and hands it to the newest remaining client when the focused
 *   one leaves the list
 */

#include <stdlib.h>
#include <string.h>

#include "src/components/client-list.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-log.h"
#include "src/core/wm-xcb.h"
#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"

#include "src/actions/action-registry.h"
#include "src/components/focus.h"

/*
 * Cached FocusSM template - created once at init, reused for all clients.
 * Avoids repeated malloc/free per client.
 */
static SMTemplate* cached_focus_template = NULL;

/*
 * The init and the on-demand accessor build through this pointer, so a test can
 * hand them a creator that returns NULL and prove what a refused table does.
 * Production leaves it at the real creator.
 */
static SMTemplate* (*focus_template_create)(void) = focus_sm_template_create;

#ifdef WM_HUB_TESTING
void
focus_component_set_template_creator_for_tests(SMTemplate* (*creator)(void) )
{
  focus_template_create = creator != NULL ? creator : focus_sm_template_create;
}
#endif

/*
 * Global component instance
 */
static FocusComponent focus_component = {
  .base = {
           .name                  = FOCUS_COMPONENT_NAME,
           .requests              = NULL, /* Focus is driven by XCB events, not requests */
      .accepted_target_names = (const char*[]) { "client", NULL },
           .accepted_targets      = NULL,
           .executor              = NULL,
           .registered            = false,
           },
  .initialized    = false,
  .focused_window = 0,
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
  static_init_done                = true;
  focus_component.initialized     = false;
  focus_component.base.registered = false;
  focus_component.focused_window  = 0;
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
focus_component_reset(void)
{
  focus_component.initialized    = false;
  focus_component.focused_window = 0;
}

/*
 * Get the focus component instance
 */
FocusComponent*
focus_component_get(void)
{
  return &focus_component;
}

/*
 * Check if component is initialized
 */
bool
focus_component_is_initialized(void)
{
  return focus_component.initialized;
}

/*
 * SM Event Emitter
 *
 * This function is passed to sm_create() to handle event emission.
 * It emits events with the correct target (client window ID).
 */
static void
focus_sm_emit(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) sm;
  (void) from_state;

  Client* c = (Client*) userdata;
  if (c == NULL)
    return;

  EventType event = (to_state == FOCUS_STATE_FOCUSED)
                        ? EVT_CLIENT_FOCUSED
                        : EVT_CLIENT_UNFOCUSED;

  hub_emit(event, c->window, NULL);
}

/*
 * Guard functions
 */

/*
 * Check if a client can receive focus.
 * Returns true for managed, focusable clients.
 */
bool
focus_guard_can_focus(StateMachine* sm, void* data)
{
  (void) data;

  if (sm == NULL || sm_get_owner(sm) == NULL)
    return false;

  Client* c = (Client*) sm_get_owner(sm);
  return client_is_managed(c) && c->focusable;
}

/*
 * Check if a client can lose focus.
 * Always returns true.
 */
bool
focus_guard_can_unfocus(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

/*
 * State Machine Template
 */
SMTemplate*
focus_sm_template_create(void)
{
  /* Define states */
  static uint32_t states[] = {
    FOCUS_STATE_UNFOCUSED,
    FOCUS_STATE_FOCUSED,
  };

  /* Define transitions:
   * UNFOCUSED → FOCUSED (on ENTER_NOTIFY, emit: EVT_CLIENT_FOCUSED)
   * FOCUSED → UNFOCUSED (on LEAVE_NOTIFY, emit: EVT_CLIENT_UNFOCUSED)
   */
  static SMTransition transitions[] = {
    {
     .from_state = FOCUS_STATE_UNFOCUSED,
     .to_state   = FOCUS_STATE_FOCUSED,
     .guard_fn   = "focus_guard_can_focus",
     .action_fn  = NULL,                  /* X operations handled by listener */
        .emit_event = EVT_CLIENT_FOCUSED,
     },
    {
     .from_state = FOCUS_STATE_FOCUSED,
     .to_state   = FOCUS_STATE_UNFOCUSED,
     .guard_fn   = "focus_guard_can_unfocus",
     .action_fn  = NULL, /* X operations handled by listener */
        .emit_event = EVT_CLIENT_UNFOCUSED,
     },
  };

  SMTemplate* tmpl = sm_template_create(
      "focus",
      states,
      2,
      transitions,
      2,
      FOCUS_STATE_UNFOCUSED);

  if (tmpl == NULL) {
    LOG_ERROR("Failed to create focus state machine template");
    return NULL;
  }

  return tmpl;
}

/*
 * State Machine Accessors
 */

/*
 * Get or create the focus state machine for a client.
 */
StateMachine*
focus_get_sm(Client* c)
{
  if (c == NULL)
    return NULL;

  StateMachine* sm = client_get_sm(c, FOCUS_COMPONENT_NAME);
  if (sm != NULL)
    return sm;

  /* Use cached template if available, otherwise create one */
  SMTemplate* tmpl = cached_focus_template;
  if (tmpl == NULL) {
    tmpl = focus_template_create();
    if (tmpl == NULL) {
      LOG_ERROR("Failed to create focus SM template for client");
      return NULL;
    }
  }

  sm = sm_create(c, tmpl, focus_sm_emit, c);
  if (sm == NULL) {
    LOG_ERROR("Failed to create focus SM instance for client");
    return NULL;
  }

  /* Store in client */
  if (!client_set_sm(c, FOCUS_COMPONENT_NAME, sm)) {
    LOG_ERROR("Failed to store focus SM for client");
    sm_destroy(sm);
    return NULL;
  }

  LOG_DEBUG("Created focus SM for client window=%u", c->window);
  return sm;
}

/*
 * Check if a client is in focus state
 */
bool
focus_is_focused(Client* c)
{
  if (c == NULL)
    return false;

  StateMachine* sm = client_get_sm(c, FOCUS_COMPONENT_NAME);
  if (sm == NULL)
    return false;

  return sm_get_state(sm) == FOCUS_STATE_FOCUSED;
}

/*
 * Get current focus state for a client
 */
FocusState
focus_get_state(Client* c)
{
  if (c == NULL)
    return FOCUS_STATE_UNFOCUSED;

  StateMachine* sm = client_get_sm(c, FOCUS_COMPONENT_NAME);
  if (sm == NULL)
    return FOCUS_STATE_UNFOCUSED;

  return (FocusState) sm_get_state(sm);
}

/*
 * Set focus state directly (for raw_write from listeners).
 * Does not run guards - use for authoritative external state changes.
 */
void
focus_set_state(Client* c, FocusState state)
{
  if (c == NULL)
    return;

  StateMachine* sm = focus_get_sm(c);
  if (sm == NULL) {
    LOG_WARN("Cannot set focus state: failed to get/create SM");
    return;
  }

  sm_raw_write(sm, state);

  /* Keep focused_window in sync with SM state */
  if (state == FOCUS_STATE_FOCUSED) {
    /* Unfocus any previously focused client */
    if (focus_component.focused_window != 0 &&
        focus_component.focused_window != c->window) {
      Client* prev = client_get_by_window(focus_component.focused_window);
      if (prev != NULL && prev != c) {
        StateMachine* prev_sm = client_get_sm(prev, FOCUS_COMPONENT_NAME);
        if (prev_sm != NULL) {
          sm_raw_write(prev_sm, FOCUS_STATE_UNFOCUSED);
        }
      }
    }
    focus_component.focused_window = c->window;
  } else if (focus_component.focused_window == c->window) {
    focus_component.focused_window = 0;
  }
}

/*
 * Get the currently focused client
 */
Client*
focus_get_focused_client(void)
{
  if (focus_component.focused_window == 0)
    return NULL;

  return client_get_by_window(focus_component.focused_window);
}

/*
 * Get the currently focused window
 */
xcb_window_t
focus_get_focused_window(void)
{
  return focus_component.focused_window;
}

/*
 * Get the target ID of the currently focused client.
 * This is the strong definition behind the weak fallback in action-registry.c,
 * so every action with a client target resolves through focus state.
 */
TargetID
focus_get_current_client(void)
{
  Client* c = focus_get_focused_client();
  if (c == NULL)
    return TARGET_ID_NONE;

  return c->target.id;
}

/*
 * Focus a client because something asked for it.
 * The guard runs, so an unmanaged or unfocusable client is rejected instead of
 * being focused the way a report from X would be.
 */
bool
focus_focus_client(Client* c)
{
  if (c == NULL)
    return false;

  if (focus_component.focused_window == c->window)
    return true;

  StateMachine* sm = focus_get_sm(c);
  if (sm == NULL)
    return false;

  if (!sm_transition(sm, FOCUS_STATE_FOCUSED))
    return false;

  Client* prev = client_get_by_window(focus_component.focused_window);
  if (prev != NULL && prev != c) {
    StateMachine* prev_sm = client_get_sm(prev, FOCUS_COMPONENT_NAME);
    if (prev_sm != NULL)
      sm_transition(prev_sm, FOCUS_STATE_UNFOCUSED);
  }

  focus_component.focused_window = c->window;
  return true;
}

/*
 * Actions
 *
 * Focus state belongs to this component, so the actions that change it are
 * registered here rather than in the component that happens to bind a key.
 */

/*
 * A client is a candidate when the WM would put focus there: managed, able to
 * take focus, and mapped (that is, on a tag the monitor is showing).
 */
static bool
focus_can_take_focus(const Client* c)
{
  return c != NULL && client_is_managed(c) && c->focusable && client_is_mapped(c);
}

/*
 * Tell the X server which window receives the keys. The state machine has
 * already moved; this is the half the user feels, because the server keeps
 * sending keys to the window it was told about last, whatever the WM decided.
 *
 * RevertToPointerRoot is dwm's choice too: when the focused window goes away
 * without the WM hearing about it, focus returns to the window under the
 * pointer instead of staying on a window that may not exist any more.
 */
static void
focus_apply_window(xcb_window_t window)
{
  if (dpy == NULL)
    return;

  xcb_set_input_focus(dpy, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);
}

/*
 * Check whether any client holds focus right now.
 *
 * Read from the state machines rather than from focused_window: an unfocus
 * event is emitted after the machine has moved, and the component's tracked
 * window is only updated by the caller afterwards.
 */
static bool
focus_any_client_focused(void)
{
  for (Client* c = client_list_get_head(); c != NULL; c = client_get_next(c)) {
    StateMachine* sm = client_get_sm(c, FOCUS_COMPONENT_NAME);
    if (sm != NULL && sm_get_state(sm) == FOCUS_STATE_FOCUSED)
      return true;
  }

  return false;
}

/*
 * Apply a focus transition to X.
 *
 * FOCUSED points the server at that window. UNFOCUSED hands it back to the
 * root window, but only when no client holds focus: taking focus away from one
 * client and giving it to another emits the new FOCUSED event first, and the
 * server must not be pointed back at the root behind it.
 */
static void
focus_on_sm_event(Event e)
{
  if (e.type == EVT_CLIENT_FOCUSED) {
    focus_apply_window((xcb_window_t) e.target);
    return;
  }

  if (!focus_any_client_focused())
    focus_apply_window(root);
}

/*
 * A newly managed client takes focus, the way dwm focuses in manage().
 *
 * Nothing else would ever point X at a window that was mapped after the WM
 * started: focus follows the pointer, and the pointer was already somewhere.
 */
static void
focus_on_client_managed(Event e)
{
  Client* c = client_get_by_window((xcb_window_t) e.target);

  if (!focus_can_take_focus(c))
    return;

  focus_focus_client(c);
}

/*
 * Pick the client focus falls back to when `skip` leaves the list: the newest
 * one that can take it, which is the head of the list.
 */
static Client*
focus_candidate_excluding(xcb_window_t skip)
{
  for (Client* c = client_list_get_head(); c != NULL; c = client_get_next(c)) {
    if (c->window != skip && focus_can_take_focus(c))
      return c;
  }

  return NULL;
}

/*
 * The focused client was unmanaged or destroyed.
 *
 * Hand focus to the newest remaining client, or back to the root window when
 * the last one goes: leaving it on a window the WM no longer manages is how a
 * keystroke ends up in a window the user cannot see.
 */
static void
focus_on_client_gone(Event e)
{
  xcb_window_t gone = (xcb_window_t) e.target;

  if (gone == 0 || gone != focus_component.focused_window)
    return;

  Client* c = client_get_by_window(gone);
  if (c != NULL) {
    StateMachine* sm = client_get_sm(c, FOCUS_COMPONENT_NAME);
    if (sm != NULL && sm_get_state(sm) == FOCUS_STATE_FOCUSED)
      sm_raw_write(sm, FOCUS_STATE_UNFOCUSED);
  }

  focus_component.focused_window = 0;

  Client* next = focus_candidate_excluding(gone);
  if (next != NULL) {
    focus_focus_client(next);
    return;
  }

  focus_apply_window(root);
}

/*
 * Follow the focus component's own state machine in X, and react to the client
 * list events that decide who is focused. Both arrive through the hub, so the
 * component keeps owning focus state and nobody has to call it.
 */
static void
focus_subscribe_events(void)
{
  hub_subscribe(EVT_CLIENT_FOCUSED, focus_on_sm_event, NULL);
  hub_subscribe(EVT_CLIENT_UNFOCUSED, focus_on_sm_event, NULL);
  hub_subscribe(EVT_CLIENT_MANAGED, focus_on_client_managed, NULL);
  hub_subscribe(EVT_CLIENT_UNMANAGED, focus_on_client_gone, NULL);
  hub_subscribe(EVT_CLIENT_DESTROYED, focus_on_client_gone, NULL);
}

static void
focus_unsubscribe_events(void)
{
  hub_unsubscribe(EVT_CLIENT_FOCUSED, focus_on_sm_event);
  hub_unsubscribe(EVT_CLIENT_UNFOCUSED, focus_on_sm_event);
  hub_unsubscribe(EVT_CLIENT_MANAGED, focus_on_client_managed);
  hub_unsubscribe(EVT_CLIENT_UNMANAGED, focus_on_client_gone);
  hub_unsubscribe(EVT_CLIENT_DESTROYED, focus_on_client_gone);
}

static Client*
focus_current_candidate(void)
{
  for (Client* c = client_list_get_head(); c != NULL; c = client_get_next(c)) {
    if (focus_can_take_focus(c))
      return c;
  }

  return NULL;
}

/*
 * Walk from `from` toward the older clients - the list runs newest first, so
 * `next` is older - and wrap around to the newest at the oldest end, so
 * focus-prev still moves when the focused client is the last one.
 */
static Client*
focus_previous_candidate(Client* from)
{
  if (from == NULL)
    return focus_current_candidate();

  for (Client* c = from;;) {
    c = client_get_next(c);
    if (c == NULL)
      c = client_list_get_head();
    if (c == NULL || c == from)
      break;
    if (focus_can_take_focus(c))
      return c;
  }

  return focus_can_take_focus(from) ? from : NULL;
}

static bool
focus_action_focus_current(ActionInvocation* inv)
{
  (void) inv;

  Client* c = focus_current_candidate();
  if (c == NULL) {
    LOG_DEBUG("focus.focus-current: no client can take focus");
    return false;
  }

  return focus_focus_client(c);
}

static bool
focus_action_focus_prev(ActionInvocation* inv)
{
  (void) inv;

  Client* c = focus_previous_candidate(focus_get_focused_client());
  if (c == NULL) {
    LOG_DEBUG("focus.focus-prev: no client can take focus");
    return false;
  }

  return focus_focus_client(c);
}

static Action focus_current_action = {
  .name            = "focus.focus-current",
  .description     = "Focus the most recently managed visible client",
  .callback        = focus_action_focus_current,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_NONE,
  .target_required = false,
  .userdata        = NULL,
};

static Action focus_prev_action = {
  .name            = "focus.focus-prev",
  .description     = "Focus the visible client before the focused one",
  .callback        = focus_action_focus_prev,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_NONE,
  .target_required = false,
  .userdata        = NULL,
};

/*
 * Register the focus actions, before the component reaches the hub, so a
 * half-initialized component is never visible to anyone.
 */
static bool
focus_register_actions(void)
{
  if (!action_register(&focus_current_action)) {
    LOG_ERROR("Failed to register action '%s'", focus_current_action.name);
    return false;
  }

  if (!action_register(&focus_prev_action)) {
    LOG_ERROR("Failed to register action '%s'", focus_prev_action.name);
    action_unregister(focus_current_action.name);
    return false;
  }

  return true;
}

static void
focus_unregister_actions(void)
{
  action_unregister(focus_current_action.name);
  action_unregister(focus_prev_action.name);
}

/*
 * XCB Event Handlers
 */

/*
 * Handle XCB_ENTER_NOTIFY event.
 * Sets focus to the entered client window.
 */
void
focus_on_enter_notify(void* event)
{
  xcb_enter_notify_event_t* e = (xcb_enter_notify_event_t*) event;

  LOG_DEBUG("ENTER_NOTIFY: window=%u, mode=%d, detail=%d",
            e->event, e->mode, e->detail);

  /* Skip if this is not a normal enter (e.g., pointer grab) */
  if (e->mode != XCB_NOTIFY_MODE_NORMAL)
    return;

  /* Skip if detail indicates different focus reason */
  if (e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
    return;

  /* Get client for this window */
  Client* c = client_get_by_window(e->event);
  if (c == NULL) {
    LOG_DEBUG("No client for enter notify window=%u", e->event);
    return;
  }

  /* Check if client is focusable */
  if (!c->focusable || !client_is_managed(c)) {
    LOG_DEBUG("Client window=%u is not focusable or not managed", e->event);
    return;
  }

  /* Get or create the SM (on-demand) */
  StateMachine* sm = focus_get_sm(c);
  if (sm == NULL) {
    LOG_WARN("Focus listener: failed to get SM for client window=%u", c->window);
    return;
  }

  /* Get current SM state */
  FocusState current = (FocusState) sm_get_state(sm);

  /* Only transition if not already focused */
  if (current == FOCUS_STATE_UNFOCUSED) {
    /* First, unfocus the previously focused client if any */
    if (focus_component.focused_window != 0 &&
        focus_component.focused_window != c->window) {
      Client* prev = client_get_by_window(focus_component.focused_window);
      if (prev != NULL && prev != c) {
        StateMachine* prev_sm = client_get_sm(prev, FOCUS_COMPONENT_NAME);
        if (prev_sm != NULL) {
          FocusState prev_state = (FocusState) sm_get_state(prev_sm);
          if (prev_state == FOCUS_STATE_FOCUSED) {
            LOG_DEBUG("Focus: unfocusing previous client window=%u",
                      focus_component.focused_window);
            sm_raw_write(prev_sm, FOCUS_STATE_UNFOCUSED);
          }
        }
      }
    }

    /* Now focus the new client */
    LOG_DEBUG("Focus: setting focus to client window=%u", c->window);
    sm_raw_write(sm, FOCUS_STATE_FOCUSED);
    focus_component.focused_window = c->window;
  } else {
    /* Already focused, just update the tracked window */
    focus_component.focused_window = c->window;
  }
}

/*
 * Handle XCB_LEAVE_NOTIFY event.
 * Clears focus when mouse leaves a client window.
 */
void
focus_on_leave_notify(void* event)
{
  xcb_leave_notify_event_t* e = (xcb_leave_notify_event_t*) event;

  LOG_DEBUG("LEAVE_NOTIFY: window=%u, mode=%d, detail=%d",
            e->event, e->mode, e->detail);

  /* Skip if this is not a normal leave (e.g., pointer grab) */
  if (e->mode != XCB_NOTIFY_MODE_NORMAL)
    return;

  /* Skip if detail indicates different focus reason */
  if (e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
    return;

  /* Get client for this window */
  Client* c = client_get_by_window(e->event);
  if (c == NULL) {
    LOG_DEBUG("No client for leave notify window=%u", e->event);
    return;
  }

  /* Get the SM */
  StateMachine* sm = client_get_sm(c, FOCUS_COMPONENT_NAME);
  if (sm == NULL) {
    /* SM not yet created, nothing to unfocus */
    return;
  }

  /* Get current SM state */
  FocusState current = (FocusState) sm_get_state(sm);

  /* Only transition if currently focused */
  if (current == FOCUS_STATE_FOCUSED) {
    LOG_DEBUG("Focus: clearing focus for client window=%u", c->window);
    sm_raw_write(sm, FOCUS_STATE_UNFOCUSED);

    /* Clear focused window if this was the focused client */
    if (focus_component.focused_window == c->window) {
      focus_component.focused_window = 0;
    }
  }
}

/*
 * Component initialization
 */
bool
focus_component_init(void)
{
  /* Check if already registered in hub */
  HubComponent* existing = hub_get_component_by_name(FOCUS_COMPONENT_NAME);
  if (existing != NULL) {
    if (focus_component.initialized) {
      LOG_DEBUG("Focus component already initialized and registered");
      return true;
    }
    LOG_DEBUG("Component registered with hub, completing initialization");
    focus_subscribe_events();
    focus_component.initialized = true;
    return true;
  }

  /* Reset if inconsistent state */
  if (focus_component.initialized) {
    LOG_DEBUG("Focus component in inconsistent state, resetting");
    focus_component_reset();
  }

  LOG_DEBUG("Initializing focus component");

  /* Register guards with SM registry */
  sm_register_guard("focus_guard_can_focus", focus_guard_can_focus);
  sm_register_guard("focus_guard_can_unfocus", focus_guard_can_unfocus);

  if (!focus_register_actions()) {
    sm_unregister_guard("focus_guard_can_focus");
    sm_unregister_guard("focus_guard_can_unfocus");
    return false;
  }

  /* Register with hub */
  if (hub_register_component(&focus_component.base) != 0) {
    sm_unregister_guard("focus_guard_can_focus");
    sm_unregister_guard("focus_guard_can_unfocus");
    focus_unregister_actions();
    return false;
  }

  focus_subscribe_events();

  /* Register XCB handlers for ENTER_NOTIFY and LEAVE_NOTIFY */
  int result = 0;

  result |= xcb_handler_register(
      XCB_ENTER_NOTIFY,
      &focus_component.base,
      focus_on_enter_notify);

  result |= xcb_handler_register(
      XCB_LEAVE_NOTIFY,
      &focus_component.base,
      focus_on_leave_notify);

  if (result != 0) {
    LOG_ERROR("Failed to register some XCB handlers for focus component");
    /* Clean up any handlers that were already registered */
    xcb_handler_unregister_component(&focus_component.base);
    /* Unregister guards and actions registered before the failure */
    sm_unregister_guard("focus_guard_can_focus");
    sm_unregister_guard("focus_guard_can_unfocus");
    focus_unregister_actions();
    focus_unsubscribe_events();
    /* Unregister from hub since we're not fully initialized */
    hub_unregister_component(FOCUS_COMPONENT_NAME);
    focus_component_reset();
    return false;
  }

  /* Cache the template for future clients */
  cached_focus_template = focus_template_create();
  if (cached_focus_template == NULL) {
    LOG_ERROR("Failed to create the focus SM template; focus component not initialized");
    xcb_handler_unregister_component(&focus_component.base);
    focus_unsubscribe_events();
    hub_unregister_component(FOCUS_COMPONENT_NAME);
    sm_unregister_guard("focus_guard_can_focus");
    sm_unregister_guard("focus_guard_can_unfocus");
    focus_unregister_actions();
    focus_component_reset();
    return false;
  }

  focus_component.initialized = true;
  LOG_DEBUG("Focus component initialized successfully");

  return true;
}

/*
 * Component shutdown
 */
void
focus_component_shutdown(void)
{
  if (!focus_component.initialized) {
    return;
  }

  LOG_DEBUG("Shutting down focus component");

  /* Unregister XCB handlers */
  xcb_handler_unregister_component(&focus_component.base);

  focus_unsubscribe_events();

  /* Unregister from hub */
  hub_unregister_component(FOCUS_COMPONENT_NAME);

  /* Unregister guards and actions */
  sm_unregister_guard("focus_guard_can_focus");
  sm_unregister_guard("focus_guard_can_unfocus");
  focus_unregister_actions();

  /* Free cached template */
  if (cached_focus_template != NULL) {
    sm_template_destroy(cached_focus_template);
    cached_focus_template = NULL;
  }

  /* Clear focused window */
  focus_component.focused_window = 0;

  focus_component.initialized = false;
  LOG_DEBUG("Focus component shutdown complete");
}