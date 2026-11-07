/*
 * Floating Component
 *
 * Takes a client out of the tiled layout and puts it back, using the state
 * machine pattern.
 *
 * Component lifecycle:
 * - on_init(): register the executor, the guards and actions, and the
 *   `floating.toggle` action a keybinding invokes
 * - executor: handle REQ_CLIENT_FLOAT and REQ_CLIENT_TILE by transitioning the
 *   client's machine; the transition's actions save and restore the geometry
 *
 * Events Emitted:
 * - EVT_CLIENT_FLOATED - the client left the layout
 * - EVT_CLIENT_TILED   - the client returned to the layout
 * - EVT_FLOATING_FAILED - a request was refused, so nothing moved
 *
 * The tiling component reads a client's machine to decide whether it belongs in
 * the layout, and subscribes to these events to arrange the monitor again.
 */

#ifndef _COMPONENT_FLOATING_H_
#define _COMPONENT_FLOATING_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "src/core/wm-hub.h"
#include "src/event-types.h"
#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"

/*
 * Component name
 */
#define FLOATING_COMPONENT_NAME "floating"

/*
 * Floating State Machine states
 */
typedef enum FloatingState {
  FLOATING_STATE_TILED    = 0,
  FLOATING_STATE_FLOATING = 1,
} FloatingState;

/*
 * Floating events emitted on state transitions
 */
typedef enum FloatingEvent {
  EVT_CLIENT_FLOATED  = EVT_FLOATING_BASE + 0,
  EVT_CLIENT_TILED    = EVT_FLOATING_BASE + 1,
  EVT_FLOATING_FAILED = EVT_FLOATING_BASE + 2,
} FloatingEvent;

/*
 * The geometry a client had when it left the layout.
 *
 * It lives in the client's machine rather than in a field on Client, so the
 * window that recorded it also frees it, and a client that never floated
 * carries nothing.
 */
typedef struct FloatingSavedGeometry {
  int16_t  x;
  int16_t  y;
  uint16_t width;
  uint16_t height;
} FloatingSavedGeometry;

/*
 * Floating component structure
 */
typedef struct FloatingComponent {
  /* Base component interface */
  HubComponent base;

  /* Component-specific state */
  bool initialized;
} FloatingComponent;

/*
 * Get the floating component instance.
 */
FloatingComponent* floating_component_get(void);

/*
 * Check if component is initialized (for testing)
 */
bool floating_component_is_initialized(void);

/*
 * Component initialization and shutdown
 */

/*
 * Initialize the floating component.
 * Registers the guards and actions with the SM registry, the executor with the
 * hub, and the `floating.toggle` action. Fails without the action registry: an
 * init that came up without its action would leave the key doing nothing for
 * the whole session.
 */
bool floating_component_init(void);

/*
 * Shutdown the floating component.
 */
void floating_component_shutdown(void);

/*
 * State Machine Template
 */

/*
 * Create the Floating State Machine template.
 *
 * Template states:
 *   FLOATING_STATE_TILED    (initial)
 *   FLOATING_STATE_FLOATING
 *
 * Transitions:
 *   TILED → FLOATING (guard: can_float, action: enter_floating, emit: EVT_CLIENT_FLOATED)
 *   FLOATING → TILED (guard: can_tile, action: exit_floating, emit: EVT_CLIENT_TILED)
 *
 * The enter and exit actions are what save and restore the geometry, so a
 * transition a guard refused leaves the window where it is.
 */
SMTemplate* floating_sm_template_create(void);

/*
 * State Machine Accessors
 */

/*
 * Get the floating state machine for a client.
 * Creates the SM on first access (on-demand allocation).
 */
StateMachine* floating_get_sm(Client* c);

/*
 * Check if a client is out of the tiled layout.
 */
bool floating_is_floating(Client* c);

/*
 * Get the current floating state for a client.
 * Returns FLOATING_STATE_TILED if no SM exists.
 */
FloatingState floating_get_state(Client* c);

/*
 * The geometry saved when the client floated, or NULL when it never floated.
 */
const FloatingSavedGeometry* floating_saved_geometry(Client* c);

/*
 * Guard Functions (for SM transitions)
 * Registered with the SM registry.
 */

/*
 * Check if a client can leave the tiled layout.
 * Returns false for a client the WM does not manage: there is no slot to save.
 */
bool floating_guard_can_float(StateMachine* sm, void* data);

/*
 * Check if a client can rejoin the tiled layout.
 * Always returns true.
 */
bool floating_guard_can_tile(StateMachine* sm, void* data);

/*
 * Action Functions (for SM transitions)
 * Registered with the SM registry.
 */

/*
 * Action for entering floating: saves the geometry the layout gave the client.
 */
bool floating_action_enter_floating(StateMachine* sm, void* data);

/*
 * Action for leaving floating: restores the saved geometry on the window.
 */
bool floating_action_exit_floating(StateMachine* sm, void* data);

#endif /* _COMPONENT_FLOATING_H_ */
