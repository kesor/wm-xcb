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
typedef struct SMHookList   SMHookList;

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
 * StateMachine structure
 * Instance of a state machine template.
 */
struct StateMachine {
  const char* name;               /* instance name (from template) */
  uint32_t    current_state;      /* current state value */
  void*       owner;              /* owner target (client, monitor, etc.) */
  SMTemplate* template;           /* reference to the template */
  void*       data;               /* instance-specific data */
  SMHookList* hooks[SM_HOOK_MAX]; /* hook lists for each phase */
};

/*
 * Create a new StateMachine instance.
 * Lazy allocation - creates the instance but doesn't initialize
 * template-owned resources until needed.
 * Returns NULL on allocation failure.
 */
StateMachine* sm_create(void* owner, SMTemplate* template);

/*
 * Destroy a StateMachine instance.
 * Frees the instance but not the template (caller manages template).
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
 * Check if a transition is valid (exists and would pass guard).
 * Does not execute the transition.
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

#endif /* _SM_INSTANCE_H_ */