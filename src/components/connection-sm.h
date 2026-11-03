/*
 * Connection State Machine Template
 *
 * Tracks the connection state of a Monitor (RandR output).
 *
 * State machine: connection
 *   DISCONNECTED -> CONNECTED     event EVT_MONITOR_CONNECTED
 *   CONNECTED    -> DISCONNECTED  event EVT_MONITOR_DISCONNECTED
 *
 * No guards: RandR says the output is there or it is not, and the machine does
 * not get a vote.
 */

#ifndef _CONNECTION_SM_H_
#define _CONNECTION_SM_H_

#include <stdint.h>

#include "../event-types.h"
#include "../sm/sm-template.h"

/*
 * Connection State Machine states
 */
typedef enum ConnectionState {
  CONNECTION_STATE_DISCONNECTED = 0,
  CONNECTION_STATE_CONNECTED    = 1,
} ConnectionState;

/*
 * Connection State Machine events emitted on transitions
 */
typedef enum ConnectionEvent {
  EVT_MONITOR_DISCONNECTED = EVT_CONNECTION_BASE + 0, /* Monitor output disconnected */
  EVT_MONITOR_CONNECTED    = EVT_CONNECTION_BASE + 1, /* Monitor output connected/reconnected */
} ConnectionEvent;

/*
 * Create the Connection State Machine template.
 * Returns a template that can be used to create ConnectionSM instances.
 */
SMTemplate* connection_sm_template_create(void);

#endif /* _CONNECTION_SM_H_ */
