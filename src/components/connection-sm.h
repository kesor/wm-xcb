/*
 * Connection State Machine Template
 *
 * Tracks the connection state of a Monitor (RandR output).
 * States: CONNECTED, DISCONNECTED
 *
 * Transitions:
 *   CONNECTED → DISCONNECTED (on output disconnect)
 *   DISCONNECTED → CONNECTED (on output reconnect)
 */

#ifndef _CONNECTION_SM_H_
#define _CONNECTION_SM_H_

#include <stdint.h>

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
  EVT_MONITOR_DISCONNECTED = 100, /* Monitor output disconnected */
  EVT_MONITOR_CONNECTED    = 101, /* Monitor output connected/reconnected */
} ConnectionEvent;

/*
 * Create the Connection State Machine template.
 * Returns a template that can be used to create ConnectionSM instances.
 */
SMTemplate* connection_sm_template_create(void);

#endif /* _CONNECTION_SM_H_ */
