/*
 * Tiling Component
 *
 * Handles window tiling using a tiled layout algorithm.
 * Manages the LayoutSM state machine and XCB window positioning.
 *
 * Component lifecycle:
 * - on_init(): register REQ_MONITOR_TILE executor, register guards/actions
 * - executor: handle REQ_MONITOR_TILE request, tile windows on monitor
 * - listener: react to EVT_LAYOUT_CHANGED
 *
 * Layout Algorithm:
 * - Divides monitor into master area and stack
 * - Master area: first nmaster clients (default 1)
 * - Stack: remaining clients arranged below/after master
 * - mfact controls master area ratio (0.0 - 1.0)
 *
 * Events Emitted:
 * - EVT_LAYOUT_CHANGED - emitted after layout changes
 *
 * Acceptance Criteria:
 * [ ] Mod+Enter tiles current client in master area
 * [ ] Layout correctly divides monitor between master and stack
 * [ ] Window positions update via X ConfigureRequest
 * [ ] Event is emitted on layout change
 */

#ifndef _COMPONENT_TILING_H_
#define _COMPONENT_TILING_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"

/*
 * Component name
 */
#define TILING_COMPONENT_NAME "tiling"

/*
 * Layout State Machine states
 */
typedef enum LayoutState {
  LAYOUT_STATE_TILE     = 0,
  LAYOUT_STATE_MONOCLE  = 1,
  LAYOUT_STATE_FLOATING = 2,
} LayoutState;

/*
 * Layout events emitted on state transitions
 */
typedef enum LayoutEvent {
  EVT_LAYOUT_TILE_CHANGED     = 40,
  EVT_LAYOUT_MONOCLE_CHANGED  = 41,
  EVT_LAYOUT_FLOATING_CHANGED = 42,
  EVT_LAYOUT_CHANGED          = 43,
} LayoutEvent;

/*
 * Request types handled by this component
 */
enum {
  REQ_MONITOR_TILE = 20,
};

/*
 * Tiling component structure
 * Tracks layout state and manages window positioning.
 */
typedef struct TilingComponent {
  /* Base component interface */
  HubComponent base;

  /* Component-specific state */
  bool initialized;

  /* Default master factor (0.5 = 50% master, 50% stack) */
  float default_mfact;

  /* Default number of master clients */
  int default_nmaster;
} TilingComponent;

/*
 * Get the tiling component instance.
 */
TilingComponent* tiling_component_get(void);

/*
 * Check if component is initialized (for testing)
 */
bool tiling_component_is_initialized(void);

/*
 * Component initialization and shutdown
 */

/*
 * Initialize the tiling component.
 * Registers:
 * - Hub executor for REQ_MONITOR_TILE
 * - State machine guards and actions with registry
 * - Listener for layout change events
 */
bool tiling_component_init(void);

/*
 * Shutdown the tiling component.
 */
void tiling_component_shutdown(void);

/*
 * State Machine Template
 */

/*
 * Create the Layout State Machine template.
 * Returns a template that can be used to create LayoutSM instances.
 *
 * Template states:
 *   LAYOUT_STATE_TILE      (initial) - standard tiling layout
 *   LAYOUT_STATE_MONOCLE   - one window fills the screen
 *   LAYOUT_STATE_FLOATING  - windows at their stored positions
 *
 * Transitions:
 *   TILE → MONOCLE  (emit: EVT_LAYOUT_MONOCLE_CHANGED, EVT_LAYOUT_CHANGED)
 *   MONOCLE → TILE  (emit: EVT_LAYOUT_TILE_CHANGED, EVT_LAYOUT_CHANGED)
 *   TILE → FLOATING (emit: EVT_LAYOUT_FLOATING_CHANGED, EVT_LAYOUT_CHANGED)
 *   FLOATING → TILE (emit: EVT_LAYOUT_TILE_CHANGED, EVT_LAYOUT_CHANGED)
 *   MONOCLE → FLOATING (emit: EVT_LAYOUT_FLOATING_CHANGED, EVT_LAYOUT_CHANGED)
 *   FLOATING → MONOCLE (emit: EVT_LAYOUT_MONOCLE_CHANGED, EVT_LAYOUT_CHANGED)
 */
SMTemplate* layout_sm_template_create(void);

/*
 * State Machine Accessors
 * These functions manage SM instances for monitors.
 */

/*
 * Get the layout state machine for a monitor.
 * Creates the SM on first access (on-demand allocation).
 */
StateMachine* tiling_get_sm(Monitor* m);

/*
 * Get current layout state for a monitor.
 * Returns LAYOUT_STATE_TILE if no SM exists.
 */
LayoutState tiling_get_state(Monitor* m);

/*
 * Set layout state directly (for raw_write from listeners).
 */
void tiling_set_state(Monitor* m, LayoutState state);

/*
 * Tiling Functions
 */

/*
 * Tile all clients on a monitor according to current layout.
 * Divides the monitor area into master and stack regions,
 * positions clients accordingly using X ConfigureRequest.
 *
 * @param m  The monitor to tile clients on
 */
void tiling_tile_monitor(Monitor* m);

/*
 * Tile a specific client in the master area.
 * If client is already tiled, repositions based on its position.
 * If client is not tiled, adds it to the master area.
 *
 * @param c  The client to tile
 */
void tiling_tile_client(Client* c);

/*
 * Arrange clients on a monitor using the tiled layout algorithm.
 *
 * Layout algorithm:
 * 1. Calculate master area based on mfact (master factor)
 * 2. Calculate stack area (remaining space)
 * 3. For tiled clients:
 *    - Master clients: arrange vertically in master area
 *    - Stack clients: arrange in remaining area
 *
 * @param m  The monitor
 * @param master_clients  Array of clients for master area
 * @param nmaster        Number of master clients
 * @param stack_clients  Array of clients for stack area
 * @param nstack         Number of stack clients
 */
void tiling_arrange(
    Monitor* m,
    Client** master_clients,
    int      nmaster,
    Client** stack_clients,
    int      nstack);

/*
 * Calculate tiled geometry for master area.
 *
 * @param m          Monitor
 * @param nmaster    Number of master clients
 * @param mfact      Master factor (0.0 - 1.0)
 * @param x          Output: x position of master area
 * @param y          Output: y position of master area
 * @param w          Output: width of master area
 * @param h          Output: height of master area
 */
void tiling_master_geometry(
    Monitor*  m,
    int       nmaster,
    float     mfact,
    int16_t*  x,
    int16_t*  y,
    uint16_t* w,
    uint16_t* h);

/*
 * Calculate tiled geometry for stack area.
 *
 * @param m          Monitor
 * @param nmaster    Number of master clients
 * @param mfact      Master factor (0.0 - 1.0)
 * @param x          Output: x position of stack area
 * @param y          Output: y position of stack area
 * @param w          Output: width of stack area
 * @param h          Output: height of stack area
 */
void tiling_stack_geometry(
    Monitor*  m,
    int       nmaster,
    float     mfact,
    int16_t*  x,
    int16_t*  y,
    uint16_t* w,
    uint16_t* h);

/*
 * Get the default master factor.
 */
float tiling_get_default_mfact(void);

/*
 * Get the default number of master clients.
 */
int tiling_get_default_nmaster(void);

/*
 * Guard Functions (for SM transitions)
 * Registered with the SM registry.
 */

/*
 * Check if we can switch to a layout.
 * Always allows layout changes.
 */
bool tiling_guard_can_change_layout(StateMachine* sm, void* data);

/*
 * Action Functions (for SM transitions)
 * Registered with the SM registry.
 */

/*
 * Action called when layout changes.
 * Tiles all clients on the monitor.
 */
bool tiling_action_on_layout_change(StateMachine* sm, void* data);

#endif /* _COMPONENT_TILING_H_ */
