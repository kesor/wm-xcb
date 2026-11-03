#ifndef _SM_TEMPLATE_H_
#define _SM_TEMPLATE_H_

/*
 * State Machine Template
 *
 * Defines the structure of a state machine: a table of state names, a table of
 * transitions, and the state the machine starts in.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declarations */
typedef struct StateMachine StateMachine;
typedef struct SMTemplate   SMTemplate;

/*
 * A state as a template declares it.
 *
 * The name is what a dump prints, and what the doc comment next to a template
 * uses in its diagram, so the table and the diagram say the same words. A state
 * with no name still works; a dump prints its value.
 */
typedef struct SMStateName {
  uint32_t    value;
  const char* name;
} SMStateName;

/*
 * One row of a template: the state it leaves, the state it enters, and what
 * happens on the way.
 */
typedef struct SMTransition {
  uint32_t    from_state;
  uint32_t    to_state;
  const char* guard_fn;   /* name of guard function */
  const char* action_fn;  /* name of action function */
  uint32_t    emit_event; /* event to emit on transition */
} SMTransition;

/*
 * A template as a table.
 *
 * `states` lists every state the machine can be in and gives each one a name.
 * `rows` lists every allowed change. The counts come from the tables through
 * SM_TABLE_COUNT(), so there is no second place to edit when a row is added.
 *
 * sm_template_from_def() checks the table: an unknown state, a state the
 * initial state cannot reach, and a row with no event all fail it. That is what
 * makes "every reachable pair appears in the template" and "every transition
 * emits exactly one event" (PHILOSOPHY 5.3 and 5.6) mechanical.
 */
typedef struct SMTemplateDef {
  const char*         name;
  uint32_t            initial_state;
  const SMStateName*  states;
  uint32_t            num_states;
  const SMTransition* rows;
  uint32_t            num_rows;
} SMTemplateDef;

#define SM_TABLE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

/* Compile a template from two tables: states as (value, name), rows as
   (from, to, guard, action, event). */
#define SM_TEMPLATE_DEF(nm, initial, state_table, row_table) \
  {                                                          \
    .name          = (nm),                                   \
    .initial_state = (initial),                              \
    .states        = (state_table),                          \
    .num_states    = (uint32_t) SM_TABLE_COUNT(state_table), \
    .rows          = (row_table),                            \
    .num_rows      = (uint32_t) SM_TABLE_COUNT(row_table),   \
  }

/* The same, for a machine with one state and no transitions. A zero-length row
   table is a GNU extension the -pedantic build rejects, so the rows are NULL. */
#define SM_TEMPLATE_DEF_STATES(nm, initial, state_table)     \
  {                                                          \
    .name          = (nm),                                   \
    .initial_state = (initial),                              \
    .states        = (state_table),                          \
    .num_states    = (uint32_t) SM_TABLE_COUNT(state_table), \
    .rows          = NULL,                                   \
    .num_rows      = 0,                                      \
  }

/*
 * SMTemplate structure
 * Defines the blueprint for a state machine.
 */
struct SMTemplate {
  const char*         name;            /* template name */
  const SMStateName*  states;          /* declared states, with their names */
  uint32_t            num_states;      /* number of declared states */
  const SMTransition* transitions;     /* declared transitions */
  uint32_t            num_transitions; /* number of transitions */
  uint32_t            initial_state;   /* default initial state */

  /* References: one held by whoever created the template, one per live machine.
   * A component that frees its cached template at shutdown still has machines
   * on its targets, and those machines look their rows up on every transition,
   * so the struct has to outlive both. */
  uint32_t refs;
};

/*
 * Create a new SMTemplate with the given parameters.
 * Returns NULL on allocation failure.
 *
 * This is the constructor the table macros compile to; it does not check the
 * template. Prefer sm_template_from_def(), which does. A hand-built template
 * (a test of the framework itself) may hold rows with no event, so a check here
 * would reject templates that exist to exercise that case.
 *
 * The creator holds one reference. sm_create() takes another for the machine it
 * creates, and gives it back in sm_destroy().
 */
SMTemplate* sm_template_create(
    const char*         name,
    const SMStateName*  states,
    uint32_t            num_states,
    const SMTransition* transitions,
    uint32_t            num_transitions,
    uint32_t            initial_state);

/*
 * Compile a template from a table definition and check it.
 *
 * Returns NULL on allocation failure, and on a table that fails
 * sm_template_validate(), with the reason logged. A caller that checks the
 * result does not install the broken table. Component init does not check,
 * so the refusal shows up when the component next tries to create a machine.
 */
SMTemplate* sm_template_from_def(const SMTemplateDef* def);

/*
 * Drop the caller's reference to a template, and free it once no machine uses
 * it any more.
 *
 * The states and transitions arrays are owned by the caller and are not freed.
 */
void sm_template_destroy(SMTemplate* tmpl);

/*
 * Take a reference for a machine that is about to use the template.
 * Not for callers outside the state machine framework.
 */
void sm_template_retain(SMTemplate* tmpl);

/*
 * Check a template and write why it failed into `reason`.
 *
 * The checks are the state machine contract in docs/PHILOSOPHY.md section 5:
 *  - the initial state is declared (2: states are named in one table)
 *  - every state a row leaves or enters is declared (3: transitions are
 *    declared, never implied)
 *  - every row emits an event (6: every transition emits exactly one event)
 *  - every declared state is reachable from the initial state (3 again, read
 *    the other way: a state nothing can enter is dead weight, and usually a
 *    typo in a row)
 *
 * `reason` may be NULL. Returns true when the template is valid.
 */
bool sm_template_validate(const SMTemplate* tmpl, char* reason, size_t reason_size);

/*
 * Find a transition from one state to another.
 * Returns NULL if no transition exists.
 */
const SMTransition* sm_template_find_transition(
    const SMTemplate* tmpl,
    uint32_t          from_state,
    uint32_t          to_state);

/*
 * Get the name of a template.
 */
const char* sm_template_get_name(const SMTemplate* tmpl);

/*
 * Get the name of a declared state, or NULL when the state is not declared or
 * was declared without a name.
 */
const char* sm_template_state_name(const SMTemplate* tmpl, uint32_t state);

#endif /* _SM_TEMPLATE_H_ */
