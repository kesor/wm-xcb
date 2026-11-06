/*
 * A state machine's current state changes through sm_raw_write() or
 * sm_transition(), and nothing else. Built both ways for the same reason as
 * client-tracked-field.c: the control build proves the header is readable.
 */
#include "src/sm/sm.h"

void
probe(StateMachine* sm, uint32_t state)
{
#ifdef PRIVATE_STATE_CONTROL
  sm_raw_write(sm, state);
#else
  sm->current_state = state;
#endif
}
