/*
 * action-registry.h - Action registration and lookup API
 *
 * Actions are the public API of components. Components register named
 * actions in their on_init() function. The keybinding component looks
 * up actions by name and invokes them with resolved targets.
 *
 * Usage:
 *   1. Components define actions with names, descriptions, and callbacks
 *   2. Components call action_register() in their on_init()
 *   3. Keybinding component calls action_lookup() when a key is pressed
 *   4. action_invoke() resolves the target and calls the callback
 */

#ifndef _WM_ACTION_REGISTRY_H_
#define _WM_ACTION_REGISTRY_H_

#include <stdbool.h>
#include <stdint.h>

#include "wm-hub.h"

/*
 * Forward declarations
 */
typedef struct Action           Action;
typedef struct ActionInvocation ActionInvocation;

/*
 * Action callback signature
 * Returns true on success, false on failure.
 */
typedef bool (*ActionCallback)(ActionInvocation* inv);

/*
 * Target resolver function
 * Called at action invocation to determine the target.
 * Return TARGET_ID_NONE if no target available.
 */
typedef TargetID (*ActionTargetResolver)(ActionInvocation* inv);

/*
 * Target type hints for actions
 */
typedef enum ActionTargetType {
  ACTION_TARGET_NONE    = 0, /* No target needed */
  ACTION_TARGET_CURRENT = 1, /* Use component's "current" target */
  ACTION_TARGET_BOUND   = 2, /* Use target bound at registration time */
  ACTION_TARGET_ANY     = 3, /* No specific requirement */
  ACTION_TARGET_CLIENT  = 4, /* Resolves to current client */
  ACTION_TARGET_MONITOR = 5, /* Resolves to current monitor */
} ActionTargetType;

/*
 * Action invocation context
 * Passed to action callbacks with resolved target and arguments.
 */
struct ActionInvocation {
  TargetID         target;         /* Resolved target (e.g., current client) */
  void*            data;           /* Action-specific data (e.g., tag number) */
  uint64_t         correlation_id; /* For async response tracking */
  void*            userdata;       /* Per-binding userdata */
  ActionTargetType target_type;    /* Hint for target resolver */
};

/*
 * Action definition
 * Registered by components during on_init().
 */
struct Action {
  const char*          name;            /* Unique action name, e.g., "fullscreen.toggle" */
  const char*          description;     /* Human-readable description */
  ActionCallback       callback;        /* Function to call */
  ActionTargetResolver target_resolver; /* How to resolve target field */
  ActionTargetType     target_type;     /* Hint for built-in target resolution */
  bool                 target_required; /* Fail if no target available */
  void*                userdata;        /* Component-specific data */
};

/*
 * Maximum number of actions in the registry
 */
#define ACTION_REGISTRY_MAX_ACTIONS 64

/*
 * Built-in target resolvers
 */

/*
 * Resolve the currently focused client.
 * Returns TARGET_ID_NONE if no client is focused.
 */
TargetID action_resolve_current_client(void);

/*
 * Resolve the currently selected monitor.
 * Returns TARGET_ID_NONE if no monitor is selected.
 */
TargetID action_resolve_current_monitor(void);

/*
 * Get the current client from the focus component.
 * Returns NULL if no client is focused.
 */
void* action_get_current_client(void);

/*
 * Get the current monitor from the monitor manager.
 * Returns NULL if no monitor is selected.
 */
void* action_get_current_monitor(void);

/*
 * Action Registry API
 */

/*
 * Initialize the action registry.
 * Components typically call this from their init functions
 * before registering actions.
 */
void action_registry_init(void);

/*
 * Shutdown the action registry.
 * Unregisters all actions and frees resources.
 */
void action_registry_shutdown(void);

/*
 * Register an action with the action registry.
 * Returns true on success, false if name already exists or registry is full.
 */
bool action_register(Action* action);

/*
 * Unregister an action by name.
 * Returns true if found and removed, false if not found.
 */
bool action_unregister(const char* name);

/*
 * Look up an action by name.
 * Returns NULL if not found.
 */
Action* action_lookup(const char* name);

/*
 * Get all registered actions.
 * Returns a NULL-terminated array of Action pointers.
 * The array is owned by the registry and should not be modified.
 */
Action** action_get_all(void);

/*
 * Get the number of registered actions.
 */
uint32_t action_count(void);

/*
 * Invoke an action by name with the given invocation context.
 * This handles target resolution and calls the action's callback.
 *
 * Returns true if the action was found and invoked successfully.
 */
bool action_invoke(const char* name, ActionInvocation* inv);

/*
 * Check if an action with the given name exists.
 */
bool action_exists(const char* name);

#endif /* _WM_ACTION_REGISTRY_H_ */