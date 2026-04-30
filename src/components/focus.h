/*
 * Focus Component
 *
 * Handles focus state for X11 clients using the state machine pattern.
 * Manages the focus state machine for tracking which client has focus.
 *
 * Component lifecycle:
 * - on_init(): register ENTER_NOTIFY handler
 * - on_adopt(): create SM instance for client
 * - listener: raw-write to SM on XCB events
 *
 * XCB Events Handled:
 * - XCB_ENTER_NOTIFY - mouse entered client window (focus gained)
 * - XCB_LEAVE_NOTIFY - mouse left client window (focus lost)
 *
 * Events Emitted:
 * - EVT_CLIENT_FOCUSED - client gained focus
 * - EVT_CLIENT_UNFOCUSED - client lost focus
 *
 * Acceptance Criteria:
 * [x] Mouse entering client window triggers focus
 * [x] Mouse leaving client window unfocuses it
 * [x] State machine transitions correctly
 * [x] Event is emitted on focus change
 */

#ifndef _COMPONENT_FOCUS_H_
#define _COMPONENT_FOCUS_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"

/*
 * Component name
 */
#define FOCUS_COMPONENT_NAME "focus"

/*
 * Focus State Machine states
 */
typedef enum FocusState {
  FOCUS_STATE_UNFOCUSED = 0,
  FOCUS_STATE_FOCUSED   = 1,
} FocusState;

/*
 * Focus events emitted on state transitions
 */
typedef enum FocusEvent {
  EVT_CLIENT_UNFOCUSED = 30, /* Client lost focus */
  EVT_CLIENT_FOCUSED   = 31, /* Client gained focus */
} FocusEvent;

/*
 * Focus component structure
 * Tracks the focus state machine and manages focus state.
 */
typedef struct FocusComponent {
  /* Base component interface */
  HubComponent base;

  /* Component-specific state */
  bool initialized;

  /* Currently focused client window (0 if none) */
  xcb_window_t focused_window;
} FocusComponent;

/*
 * Get the focus component instance.
 */
FocusComponent* focus_component_get(void);

/*
 * Check if component is initialized (for testing)
 */
bool focus_component_is_initialized(void);

/*
 * Component initialization and shutdown
 */

/*
 * Initialize the focus component.
 * Registers:
 * - XCB handler for ENTER_NOTIFY and LEAVE_NOTIFY
 */
bool focus_component_init(void);

/*
 * Shutdown the focus component.
 */
void focus_component_shutdown(void);

/*
 * State Machine Template
 */

/*
 * Create the Focus State Machine template.
 * Returns a template that can be used to create FocusSM instances.
 *
 * Template states:
 *   FOCUS_STATE_UNFOCUSED (initial)
 *   FOCUS_STATE_FOCUSED
 *
 * Transitions:
 *   UNFOCUSED → FOCUSED (raw_write on ENTER_NOTIFY, emit: EVT_CLIENT_FOCUSED)
 *   FOCUSED → UNFOCUSED (raw_write on LEAVE_NOTIFY, emit: EVT_CLIENT_UNFOCUSED)
 */
SMTemplate* focus_sm_template_create(void);

/*
 * State Machine Accessors
 * These functions manage SM instances for clients.
 */

/*
 * Get the focus state machine for a client.
 * Creates the SM on first access (on-demand allocation).
 */
StateMachine* focus_get_sm(Client* c);

/*
 * Check if a client is in focus state.
 */
bool focus_is_focused(Client* c);

/*
 * Get current focus state for a client.
 * Returns FOCUS_STATE_UNFOCUSED if no SM exists.
 */
FocusState focus_get_state(Client* c);

/*
 * Set focus state directly (for raw_write from listeners).
 * Does not run guards - use for authoritative external state changes.
 */
void focus_set_state(Client* c, FocusState state);

/*
 * Get the currently focused client.
 * Returns NULL if no client has focus.
 */
Client* focus_get_focused_client(void);

/*
 * Get the currently focused window.
 * Returns 0 if no window has focus.
 */
xcb_window_t focus_get_focused_window(void);

/*
 * XCB Event Handlers
 * Called by the xcb_handler dispatch system.
 */

/*
 * Handle XCB_ENTER_NOTIFY event.
 * Sets focus to the entered client.
 */
void focus_on_enter_notify(void* event);

/*
 * Handle XCB_LEAVE_NOTIFY event.
 * Clears focus from the left client.
 */
void focus_on_leave_notify(void* event);

/*
 * Guard Functions (for SM transitions)
 * Registered with the SM registry.
 */

/*
 * Check if a client can receive focus.
 * Returns true for managed, focusable clients.
 */
bool focus_guard_can_focus(StateMachine* sm, void* data);

/*
 * Check if a client can lose focus.
 * Always returns true.
 */
bool focus_guard_can_unfocus(StateMachine* sm, void* data);

#endif /* _COMPONENT_FOCUS_H_ */