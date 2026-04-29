#ifndef _SM_REGISTRY_H_
#define _SM_REGISTRY_H_

/*
 * State Machine Guard and Action Registry
 *
 * Provides registration and lookup of guard and action functions
 * for state machine transitions. Components register their guards
 * and actions, and the SM framework looks them up by name.
 */

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct StateMachine StateMachine;

/*
 * Guard function type
 * Returns true if the transition is allowed, false to reject.
 * The data parameter is typically the event/request data.
 */
typedef bool (*SMGuardFn)(StateMachine* sm, void* data);

/*
 * Action function type
 * Executes side effects during transition.
 * Returns true on success, false on failure.
 */
typedef bool (*SMActionFn)(StateMachine* sm, void* data);

/*
 * Registry initialization
 */
void sm_registry_init(void);
void sm_registry_shutdown(void);

/*
 * Register a guard function
 */
void sm_register_guard(const char* name, SMGuardFn fn);

/*
 * Unregister a guard function
 */
void sm_unregister_guard(const char* name);

/*
 * Register an action function
 */
void sm_register_action(const char* name, SMActionFn fn);

/*
 * Unregister an action function
 */
void sm_unregister_action(const char* name);

/*
 * Look up a guard function by name
 * Returns NULL if not found.
 */
SMGuardFn sm_lookup_guard(const char* name);

/*
 * Look up an action function by name
 * Returns NULL if not found.
 */
SMActionFn sm_lookup_action(const char* name);

/*
 * Run a guard by name.
 * Returns true if guard passes.
 * If guard_name is non-NULL but guard is not found, returns true (failsafe).
 * This design ensures missing guards don't block transitions, with LOG_WARN
 * to alert about potential misconfiguration.
 * Set guard_name to NULL to skip guard checking (always allowed).
 */
bool sm_run_guard(StateMachine* sm, const char* guard_name, void* data);

/*
 * Run an action by name
 * Returns true if action succeeds, false if action fails or is not found.
 * Logs warning if action not found.
 */
bool sm_run_action(StateMachine* sm, const char* action_name, void* data);

#endif /* _SM_REGISTRY_H_ */