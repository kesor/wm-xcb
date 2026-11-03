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
  static const SMStateName states[] = {
    { CONNECTION_STATE_DISCONNECTED, "DISCONNECTED" },
    { CONNECTION_STATE_CONNECTED,    "CONNECTED"    },
  };

  /* No guard: a monitor is connected because RandR said so, and disconnected
     because the output went away. Either can happen at any time. */
  static const SMTransition rows[] = {
    { CONNECTION_STATE_CONNECTED,    CONNECTION_STATE_DISCONNECTED, NULL, NULL, EVT_MONITOR_DISCONNECTED },
    { CONNECTION_STATE_DISCONNECTED, CONNECTION_STATE_CONNECTED,    NULL, NULL, EVT_MONITOR_CONNECTED    },
  };

  static const SMTemplateDef def =
      SM_TEMPLATE_DEF("connection", CONNECTION_STATE_DISCONNECTED, states, rows);

  SMTemplate* tmpl = sm_template_from_def(&def);
  if (tmpl == NULL) {
    LOG_ERROR("Failed to create connection state machine template");
    return NULL;
  }

  return tmpl;
}
