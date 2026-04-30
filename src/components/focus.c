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
 */

#include <stdlib.h>
#include <string.h>

#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"

#include "focus.h"

/*
 * Cached FocusSM template - created once at init, reused for all clients.
 * Avoids repeated malloc/free per client.
 */
static SMTemplate* cached_focus_template = NULL;

/*
 * Global component instance
 */
static FocusComponent focus_component = {
  .base = {
           .name       = FOCUS_COMPONENT_NAME,
           .requests   = NULL, /* Focus is driven by XCB events, not requests */
      .targets    = (TargetType[]) { TARGET_TYPE_CLIENT, TARGET_TYPE_NONE },
           .executor   = NULL,
           .registered = false,
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

  if (sm == NULL || sm->owner == NULL)
    return false;

  Client* c = (Client*) sm->owner;
  return c->managed && c->focusable;
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

  StateMachine* sm = client_get_sm(c, "focus");
  if (sm != NULL)
    return sm;

  /* Use cached template if available, otherwise create one */
  SMTemplate* tmpl = cached_focus_template;
  if (tmpl == NULL) {
    tmpl = focus_sm_template_create();
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
  client_set_sm(c, "focus", sm);

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

  StateMachine* sm = client_get_sm(c, "focus");
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

  StateMachine* sm = client_get_sm(c, "focus");
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
        StateMachine* prev_sm = client_get_sm(prev, "focus");
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
  if (!c->focusable || !c->managed) {
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
        StateMachine* prev_sm = client_get_sm(prev, "focus");
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
  StateMachine* sm = client_get_sm(c, "focus");
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

  /* Register with hub */
  hub_register_component(&focus_component.base);

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
    /* Unregister guards that were registered before the failure */
    sm_unregister_guard("focus_guard_can_focus");
    sm_unregister_guard("focus_guard_can_unfocus");
    /* Unregister from hub since we're not fully initialized */
    hub_unregister_component(FOCUS_COMPONENT_NAME);
    focus_component_reset();
    return false;
  }

  /* Cache the template for future clients */
  cached_focus_template = focus_sm_template_create();

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

  /* Unregister from hub */
  hub_unregister_component(FOCUS_COMPONENT_NAME);

  /* Unregister guards */
  sm_unregister_guard("focus_guard_can_focus");
  sm_unregister_guard("focus_guard_can_unfocus");

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