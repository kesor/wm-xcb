#ifndef _SM_TEMPLATE_H_
#define _SM_TEMPLATE_H_

/*
 * State Machine Template
 *
 * Defines the structure of a state machine: states, transitions,
 * and initial state.
 */

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct StateMachine StateMachine;
typedef struct SMTemplate   SMTemplate;

/*
 * Transition structure
 * Represents a valid state change with optional guard and action.
 */
typedef struct SMTransition {
  uint32_t    from_state;
  uint32_t    to_state;
  const char* guard_fn;   /* name of guard function */
  const char* action_fn;  /* name of action function */
  uint32_t    emit_event; /* event to emit on transition */
} SMTransition;

/*
 * SMTemplate structure
 * Defines the blueprint for a state machine.
 */
struct SMTemplate {
  const char*   name;            /* template name */
  uint32_t*     states;          /* array of state values */
  uint32_t      num_states;      /* number of states */
  SMTransition* transitions;     /* array of valid transitions */
  uint32_t      num_transitions; /* number of transitions */
  uint32_t      initial_state;   /* default initial state */
};

/*
 * Create a new SMTemplate with the given parameters.
 * Returns NULL on allocation failure.
 */
SMTemplate* sm_template_create(
    const char*   name,
    uint32_t*     states,
    uint32_t      num_states,
    SMTransition* transitions,
    uint32_t      num_transitions,
    uint32_t      initial_state);

/*
 * Destroy an SMTemplate.
 * Frees the template struct. Note: the states and transitions arrays
 * are owned by the caller and must be freed separately if needed.
 */
void sm_template_destroy(SMTemplate* tmpl);

/*
 * Find a transition from one state to another.
 * Returns NULL if no transition exists.
 */
SMTransition* sm_template_find_transition(
    SMTemplate* tmpl,
    uint32_t    from_state,
    uint32_t    to_state);

/*
 * Get the name of a template.
 */
const char* sm_template_get_name(SMTemplate* tmpl);

#endif /* _SM_TEMPLATE_H_ */
