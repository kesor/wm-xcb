#ifndef _SM_DEBUG_H_
#define _SM_DEBUG_H_

/*
 * State machine debug dump
 *
 * The WM answers "why is this window fullscreen?" by printing every live
 * machine: who owns it, which state it is in, how often it has been in each
 * state, and its last SM_TRACE_DEPTH changes. A signal asks for it
 * (SIGUSR1, see wm-signals.h); the output goes to a stream the caller names, so
 * a test can capture it.
 *
 * The two record hooks are called by the state machine framework itself:
 * sm_create() and sm_destroy() keep the live list, and every applied change
 * bumps the entrance count of the state it entered.
 */

#include <stdint.h>
#include <stdio.h>

/* Forward declaration (defined in sm-instance.h) */
typedef struct StateMachine StateMachine;

void sm_debug_machine_created(StateMachine* sm);
void sm_debug_machine_destroyed(StateMachine* sm);

/*
 * Count one entrance into `state`: every applied change, plus the state a
 * machine is created in. A state that is not in the template's state list is
 * not counted; it was reached by a raw write the template does not declare.
 */
void sm_debug_note_enter(StateMachine* sm, uint32_t state);

/*
 * Print every live machine, oldest created first, and flush `out`.
 *
 * Reading a dump needs nothing from the X server and changes nothing: no
 * request is sent and no event is emitted.
 */
void sm_debug_dump(FILE* out);

/* Number of live machines, which is what a dump's header reports. */
uint32_t sm_debug_live_count(void);

#endif /* _SM_DEBUG_H_ */
