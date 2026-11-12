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

/* Forward declarations (defined in sm-template.h) */
typedef struct SMTemplate   SMTemplate;
typedef struct StateMachine StateMachine;

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
 * Data destructor, installed with sm_set_owned_data().
 */
typedef void (*SMDataFree)(void* data);

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
 * The StateMachine definition is in sm-instance-internal.h and is not part of
 * this header. current_state is the value the single-writer rule is about, and
 * a definition here would let any component assign it and compile. Read the
 * state with sm_get_state(); change it with sm_raw_write() or sm_transition().
 */

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
 * Frees the instance and the data it was given with sm_set_owned_data(), but
 * not the template (caller manages template).
 */
void sm_destroy(StateMachine* sm);

/*
 * Get the current state of a state machine.
 */
uint32_t sm_get_state(StateMachine* sm);

/*
 * Get the target this machine belongs to, as it was passed to sm_create().
 * This is how a guard or an action reaches its client or monitor.
 */
void* sm_get_owner(StateMachine* sm);

/*
 * Get the instance data, the value set with sm_set_data() or
 * sm_set_owned_data(). NULL when none is set.
 */
void* sm_get_data(StateMachine* sm);

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
 * Set instance-specific data that somebody else frees.
 * This is supported public API for setting sm->data. The data stays owned by
 * the caller, which must free it before the machine is destroyed.
 *
 * A machine holds one datum: whatever a previous sm_set_data() or
 * sm_set_owned_data() installed is replaced. When that previous datum was
 * installed with a destructor, the destructor runs here, before the new data is
 * stored. Passing the pointer the machine already holds only changes the
 * destructor, so the data is not freed.
 */
void sm_set_data(StateMachine* sm, void* data);

/*
 * Set instance-specific data that the machine owns.
 * `free_fn` runs on the data when sm_destroy() is called, whatever destroyed
 * the machine - the component that stored the data does not have to be
 * registered any more for the data to be freed. Pass NULL to make the machine
 * own the data without a way to free it, or use sm_set_data() for data somebody
 * else frees.
 *
 * Replacing data that an earlier call installed with a destructor runs that
 * destructor first, the same way sm_set_data() does. Passing the pointer the
 * machine already holds only changes `free_fn`.
 */
void sm_set_owned_data(StateMachine* sm, void* data, SMDataFree free_fn);

#endif /* _SM_INSTANCE_H_ */