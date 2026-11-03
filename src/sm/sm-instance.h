#ifndef _SM_INSTANCE_H_
#define _SM_INSTANCE_H_

/*
 * State Machine Instance
 *
 * A running state machine that belongs to a target (owner).
 * Tracks current state and provides transition operations.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sm-trace.h"

/* Forward declarations (defined in sm-template.h) */
typedef struct SMTemplate   SMTemplate;
typedef struct StateMachine StateMachine;
typedef struct SMHookList   SMHookList;

/*
 * Event emitter function type
 * Called when a state machine transitions.
 * Receives the SM, from_state, to_state, and emit_userdata.
 *
 * The callback is invoked for transition notification; its return value
 * is currently ignored by the state machine implementation.
 */
typedef void (*EventEmitter)(StateMachine*, uint32_t from_state, uint32_t to_state, void* emit_userdata);

/*
 * Hook phases for state machine events
 */
typedef enum SMHookPhase {
  SM_HOOK_PRE_GUARD,
  SM_HOOK_POST_GUARD,
  SM_HOOK_PRE_ACTION,
  SM_HOOK_POST_ACTION,
  SM_HOOK_PRE_EMIT,
  SM_HOOK_POST_EMIT,
  SM_HOOK_MAX,
} SMHookPhase;

/*
 * Hook function signature
 */
typedef void (*SMHookFn)(StateMachine*, void*);

/*
 * How many states per machine carry an entry count. A template with more
 * states than this still dumps; the extra states are reported as uncounted.
 */
#define SM_STATE_ENTER_MAX 8

/*
 * StateMachine structure
 * Instance of a state machine template.
 */
struct StateMachine {
  const char*  name;               /* instance name (from template) */
  uint32_t     current_state;      /* current state value */
  void*        owner;              /* owner target (client, monitor, etc.) */
  SMTemplate*  template;           /* reference to the template */
  void*        data;               /* instance-specific data */
  SMHookList*  hooks[SM_HOOK_MAX]; /* hook lists for each phase */
  EventEmitter emit;               /* event emitter function (e.g., hub_emit) */
  void*        emit_userdata;      /* userdata passed to emit function */

  /* Last SM_TRACE_DEPTH changes, for sm_trace_snapshot() and the state dump. */
  struct {
    SMTraceEntry entries[SM_TRACE_DEPTH];
    uint32_t     count; /* entries recorded, saturates at SM_TRACE_DEPTH */
    uint32_t     next;  /* slot the next entry is written to */
  } trace;

  /* How often each state was entered, by position in the template's state list.
   * A dump reads these through sm-debug.c, which is what writes them. */
  uint32_t enter_counts[SM_STATE_ENTER_MAX];
  uint32_t enter_counts_lost; /* entries for a state past SM_STATE_ENTER_MAX */

  /* Link in the live-machine list the dump walks. Owned by sm-debug.c. */
  StateMachine* next_live;
};

/*
 * Create a new StateMachine instance.
 * Allocates and initializes the instance immediately, including
 * hook-list storage and any template-specific initialization
 * performed by the template's init function.
 * Returns NULL on allocation failure.
 *
 * The emit parameter is optional and specifies the event emitter function.
 * If provided, it will be called during state transitions (including
 * raw_write) to emit events. Set emit_userdata to pass additional context.
 */
StateMachine* sm_create(
    void*        owner,
    SMTemplate*  template,
    EventEmitter emit,
    void*        emit_userdata);

/*
 * Set the event emitter for a state machine.
 * Can be called after sm_create() to configure event emission.
 */
void sm_set_emitter(StateMachine* sm, EventEmitter emit, void* emit_userdata);

/*
 * Destroy a StateMachine instance.
 * Frees the instance and drops the reference sm_create() took on the
 * template. The creator's reference is separate: the caller that created
 * the template still calls sm_template_destroy().
 */
void sm_destroy(StateMachine* sm);

/*
 * Get the current state of a state machine.
 */
uint32_t sm_get_state(StateMachine* sm);

/*
 * Raw write - set state without guards.
 * Used for authoritative state changes (e.g., from listeners).
 * Emits the template's transition event.
 */
void sm_raw_write(StateMachine* sm, uint32_t new_state);

/*
 * Transition - attempt to move to a new state.
 * Validates guard, executes action, updates state, emits event.
 * Returns true if transition succeeded, false otherwise.
 */
bool sm_transition(StateMachine* sm, uint32_t target_state);

/*
 * Check if a transition exists from current state to target state.
 * Does not evaluate guards or execute the transition.
 */
bool sm_can_transition(StateMachine* sm, uint32_t target_state);

/*
 * Get available transitions from current state.
 * Returns an array of target states (caller must free).
 * count is set to the number of states returned.
 */
uint32_t* sm_get_available_transitions(StateMachine* sm, uint32_t* count);

/*
 * Add a hook to be called at a specific phase.
 */
void sm_add_hook(
    StateMachine* sm,
    SMHookPhase   phase,
    SMHookFn      fn,
    void*         userdata);

/*
 * Remove a hook from a specific phase.
 */
void sm_remove_hook(
    StateMachine* sm,
    SMHookPhase   phase,
    SMHookFn      fn);

/*
 * Get the template associated with a state machine.
 */
SMTemplate* sm_get_template(StateMachine* sm);

/*
 * Get the name of a state machine.
 */
const char* sm_get_name(StateMachine* sm);

/*
 * Set instance-specific data.
 * This is supported public API for setting sm->data.
 */
void sm_set_data(StateMachine* sm, void* data);

#endif /* _SM_INSTANCE_H_ */