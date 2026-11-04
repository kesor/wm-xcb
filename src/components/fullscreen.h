/*
 * Fullscreen Component
 *
 * Handles fullscreen state for X11 clients using the state machine pattern.
 * Manages the fullscreen state machine and X property for _NET_WM_STATE.
 *
 * Component lifecycle:
 * - on_init(): register PropertyNotify handler, register guards/actions
 * - on_adopt(): create SM instance for client
 * - executor: handle REQ_CLIENT_FULLSCREEN by transitioning the client's
 *   machine; the transition's action issues the _NET_WM_STATE change
 *
 * XCB Events Handled:
 * - XCB_PROPERTY_NOTIFY - listen for _NET_WM_STATE changes
 *
 * Events Emitted:
 * - EVT_FULLSCREEN_ENTERED - client entered fullscreen mode
 * - EVT_FULLSCREEN_EXITED - client exited fullscreen mode
 *
 * Acceptance Criteria:
 * [x] Mod+f toggles fullscreen on current client
 * [x] State machine transitions correctly
 * [x] Event is emitted on fullscreen change
 * [x] X property reflects fullscreen state
 */

#ifndef _COMPONENT_FULLSCREEN_H_
#define _COMPONENT_FULLSCREEN_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "src/event-types.h"
#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"

/*
 * Component name
 */
#define FULLSCREEN_COMPONENT_NAME "fullscreen"

/*
 * Fullscreen State Machine states
 */
typedef enum FullscreenState {
  FULLSCREEN_STATE_WINDOWED   = 0,
  FULLSCREEN_STATE_FULLSCREEN = 1,
} FullscreenState;

/*
 * Fullscreen events emitted on state transitions
 */
typedef enum FullscreenEvent {
  EVT_FULLSCREEN_ENTERED = EVT_FULLSCREEN_BASE + 0,
  EVT_FULLSCREEN_EXITED  = EVT_FULLSCREEN_BASE + 1,
  EVT_FULLSCREEN_FAILED  = EVT_FULLSCREEN_BASE + 2,
} FullscreenEvent;

/*
 * Request types handled by this component
 * Note: Uses REQ_CLIENT_FULLSCREEN defined in wm-hub.h
 */

/*
 * Fullscreen component structure
 * Tracks the fullscreen state machine and manages X property updates.
 */
typedef struct FullscreenComponent {
  /* Base component interface */
  HubComponent base;

  /* Component-specific state */
  bool initialized;
} FullscreenComponent;

/*
 * Get the fullscreen component instance.
 */
FullscreenComponent* fullscreen_component_get(void);

/*
 * Check if component is initialized (for testing)
 */
bool fullscreen_component_is_initialized(void);

/*
 * Component initialization and shutdown
 */

/*
 * Initialize the fullscreen component.
 * Registers:
 * - XCB handler for PROPERTY_NOTIFY
 * - State machine guards and actions with registry
 */
bool fullscreen_component_init(void);

/*
 * Shutdown the fullscreen component.
 */
void fullscreen_component_shutdown(void);

/*
 * State Machine Template
 */

/*
 * Create the Fullscreen State Machine template.
 * Returns a template that can be used to create FullscreenSM instances.
 *
 * Template states:
 *   FULLSCREEN_STATE_WINDOWED    (initial)
 *   FULLSCREEN_STATE_FULLSCREEN
 *
 * Transitions:
 *   WINDOWED → FULLSCREEN (guard: can_fullscreen, action: enter_fullscreen, emit: EVT_FULLSCREEN_ENTERED)
 *   FULLSCREEN → WINDOWED (guard: can_unfullscreen, action: exit_fullscreen, emit: EVT_FULLSCREEN_EXITED)
 *
 * The enter and exit actions are what write `_NET_WM_STATE` to the window, so a
 * transition that a guard refused leaves the property untouched.
 */
SMTemplate* fullscreen_sm_template_create(void);

/*
 * State Machine Accessors
 * These functions manage SM instances for clients.
 */

/*
 * Get the fullscreen state machine for a client.
 * Creates the SM on first access (on-demand allocation).
 */
StateMachine* fullscreen_get_sm(Client* c);

/*
 * Check if a client is in fullscreen state.
 */
bool fullscreen_is_fullscreen(Client* c);

/*
 * Set fullscreen state directly (for raw_write from listeners).
 * Does not run guards - use for authoritative external state changes.
 */
void fullscreen_set_state(Client* c, FullscreenState state);

/*
 * Helper: Get current fullscreen state for a client.
 * Returns FULLSCREEN_STATE_WINDOWED if no SM exists.
 */
FullscreenState fullscreen_get_state(Client* c);

/*
 * XCB Property Update
 */

/*
 * Update the _NET_WM_STATE X property to reflect fullscreen state.
 * Called by the listener when property changes, or by the executor
 * when fullscreen state changes.
 */
void fullscreen_update_x_property(Client* c, bool fullscreen);

/*
 * PropertyNotify handler for _NET_WM_STATE monitoring.
 * Called by xcb_handler_dispatch() when a property changes on a client window.
 */
void fullscreen_on_property_notify(void* event);

/*
 * The queue of _NET_WM_STATE reads in flight, walked by the loop's poll
 * callback and by the unit tests.
 *
 * `arrived` says whether the head's reply has arrived and, when it has, hands
 * it over; the walk stops at the first head it refuses. `apply` takes the reply
 * and owns it. Applying can queue another read, which has to stay behind the
 * ones still in flight, so the head is dropped before `apply` runs.
 *
 * The split exists because a unit test has no server to make a reply arrive,
 * and the order the walk applies them in is the behaviour worth keeping.
 */
typedef bool (*FullscreenStateReadArrived)(void*                     userdata,
                                           xcb_get_property_cookie_t cookie,
                                           xcb_window_t              window,
                                           void**                    reply);
typedef void (*FullscreenStateReadApply)(void* userdata, xcb_window_t window, void* reply);

/* Returns the number of replies applied. */
size_t fullscreen_state_read_drain(FullscreenStateReadArrived arrived,
                                   FullscreenStateReadApply   apply,
                                   void*                      userdata);

#ifdef WM_HUB_TESTING
/* Queue one read without an X connection, and inspect or clear the queue. */
bool   fullscreen_state_read_queue_push_for_tests(xcb_get_property_cookie_t cookie,
                                                  xcb_window_t              window);
size_t fullscreen_state_read_queue_count_for_tests(void);
void   fullscreen_state_read_queue_reset_for_tests(void);
#endif

/*
 * Guard Functions (for SM transitions)
 * Registered with the SM registry.
 */

/*
 * Check if a client can enter fullscreen mode.
 * Returns true for managed clients.
 */
bool fullscreen_guard_can_fullscreen(StateMachine* sm, void* data);

/*
 * Check if a client can exit fullscreen mode.
 * Always returns true.
 */
bool fullscreen_guard_can_unfullscreen(StateMachine* sm, void* data);

/*
 * Action Functions (for SM transitions)
 * Registered with the SM registry.
 * Each is the transition's X side effect: it adds or removes
 * `_NET_WM_STATE_FULLSCREEN` on the machine's owner and returns false when
 * there is no owner to change it for.
 */

/*
 * Action called when entering fullscreen (via sm_transition).
 */
bool fullscreen_action_enter_fullscreen(StateMachine* sm, void* data);

/*
 * Action called when exiting fullscreen (via sm_transition).
 */
bool fullscreen_action_exit_fullscreen(StateMachine* sm, void* data);

#endif /* _COMPONENT_FULLSCREEN_H_ */
