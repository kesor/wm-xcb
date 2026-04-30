/*
 * Connection State Machine Template Implementation
 *
 * Tracks the connection state of a Monitor (RandR output).
 */

#include <stdlib.h>
#include <string.h>

#include "connection-sm.h"
#include "wm-log.h"

/*
 * Guard functions
 *
 * Guard functions must match the SMGuardFn signature:
 * bool (*)(StateMachine* sm, void* data)
 */
static bool
guard_disconnect_allowed(StateMachine* sm, void* data)
{
  /* Always allow disconnect */
  (void) sm;
  (void) data;
  return true;
}

static bool
guard_connect_allowed(StateMachine* sm, void* data)
{
  /* Always allow connect/reconnect */
  (void) sm;
  (void) data;
  return true;
}

/*
 * Template creation
 */
SMTemplate*
connection_sm_template_create(void)
{
  /* Define states */
  static uint32_t states[] = {
    CONNECTION_STATE_DISCONNECTED,
    CONNECTION_STATE_CONNECTED,
  };

  /* Define transitions:
   * CONNECTED → DISCONNECTED (on output disconnect)
   * DISCONNECTED → CONNECTED (on output reconnect)
   *
   * Note: guard_fn is set to NULL since we always allow these transitions.
   * The SM registry's sm_run_guard() returns true for NULL guards (failsafe).
   * If guards are needed later, register them with sm_register_guard().
   */
  static SMTransition transitions[] = {
    {
     .from_state = CONNECTION_STATE_CONNECTED,
     .to_state   = CONNECTION_STATE_DISCONNECTED,
     .guard_fn   = NULL,                          /* always allowed */
        .action_fn  = NULL,
     .emit_event = EVT_MONITOR_DISCONNECTED,
     },
    {
     .from_state = CONNECTION_STATE_DISCONNECTED,
     .to_state   = CONNECTION_STATE_CONNECTED,
     .guard_fn   = NULL, /* always allowed */
        .action_fn  = NULL,
     .emit_event = EVT_MONITOR_CONNECTED,
     },
  };

  /* Create template */
  SMTemplate* tmpl = sm_template_create(
      "connection",
      states,
      2, /* num_states */
      transitions,
      2, /* num_transitions */
      CONNECTION_STATE_DISCONNECTED /* initial_state */);

  if (tmpl == NULL) {
    LOG_ERROR("Failed to create connection state machine template");
    return NULL;
  }

  return tmpl;
}
