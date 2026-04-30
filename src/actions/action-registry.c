/*
 * action-registry.c - Action registration and lookup implementation
 *
 * Implements the action registry for component action exposure.
 */

#include "action-registry.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wm-log.h"

/*
 * Maximum number of actions in the registry
 */
#define MAX_ACTIONS 64

/*
 * Action storage
 */
static Action*  actions[MAX_ACTIONS];
static uint32_t actions_allocated = 0;
static bool     initialized       = false;

/*
 * Built-in target resolver: current client
 * Uses weak linkage - provided by focus component if present
 */
extern TargetID focus_get_current_client(void);
__attribute__((weak)) TargetID
focus_get_current_client(void)
{
  return TARGET_ID_NONE;
}

/*
 * Get focused client (weak linkage)
 */
extern void* focus_get_focused_client(void);
__attribute__((weak)) void*
focus_get_focused_client(void)
{
  return NULL;
}

/*
 * Built-in target resolver: current monitor
 * Uses weak linkage - provided by monitor manager if present
 */
extern TargetID monitor_get_current_monitor(void);
__attribute__((weak)) TargetID
monitor_get_current_monitor(void)
{
  return TARGET_ID_NONE;
}

/*
 * Get selected monitor (weak linkage)
 */
extern void* monitor_get_selected(void);
__attribute__((weak)) void*
monitor_get_selected(void)
{
  return NULL;
}

/*
 * Built-in target resolvers
 */
TargetID
action_resolve_current_client(void)
{
  return focus_get_current_client();
}

TargetID
action_resolve_current_monitor(void)
{
  return monitor_get_current_monitor();
}

void*
action_get_current_client(void)
{
  return focus_get_focused_client();
}

void*
action_get_current_monitor(void)
{
  return monitor_get_selected();
}

/*
 * Resolve target based on action's target_type hint
 */
static TargetID
resolve_target(Action* action, ActionInvocation* inv)
{
  if (action->target_resolver != NULL) {
    /* Custom resolver provided by action */
    return action->target_resolver(inv);
  }

  switch (action->target_type) {
  case ACTION_TARGET_CLIENT:
    return action_resolve_current_client();
  case ACTION_TARGET_MONITOR:
    return action_resolve_current_monitor();
  case ACTION_TARGET_CURRENT:
    /* Default: resolve current client */
    return action_resolve_current_client();
  case ACTION_TARGET_BOUND:
    /* Use bound target from invocation */
    if (inv != NULL && inv->target != TARGET_ID_NONE) {
      return inv->target;
    }
    break;
  case ACTION_TARGET_ANY:
    /* Any target is fine */
    if (inv != NULL && inv->target != TARGET_ID_NONE) {
      return inv->target;
    }
    break;
  case ACTION_TARGET_NONE:
  default:
    /* No target needed */
    return TARGET_ID_NONE;
  }

  return TARGET_ID_NONE;
}

void
action_registry_init(void)
{
  if (initialized) {
    LOG_WARN("Action registry already initialized");
    return;
  }

  LOG_DEBUG("Initializing action registry");
  memset(actions, 0, sizeof(actions));
  actions_allocated = 0;
  initialized       = true;
}

void
action_registry_shutdown(void)
{
  if (!initialized) {
    return;
  }

  LOG_DEBUG("Shutting down action registry");

  /* Unregister all actions */
  actions_allocated = 0;
  memset(actions, 0, sizeof(actions));

  initialized = false;
}

bool
action_register(Action* action)
{
  if (!initialized) {
    LOG_ERROR("Action registry not initialized");
    return false;
  }

  if (action == NULL) {
    LOG_ERROR("Cannot register NULL action");
    return false;
  }

  if (action->name == NULL || action->name[0] == '\0') {
    LOG_ERROR("Action name cannot be empty");
    return false;
  }

  if (action->callback == NULL) {
    LOG_ERROR("Action '%s' has no callback", action->name);
    return false;
  }

  /* Check for duplicate name */
  if (action_lookup(action->name) != NULL) {
    LOG_WARN("Action '%s' already registered", action->name);
    return false;
  }

  /* Check for space */
  if (actions_allocated >= MAX_ACTIONS) {
    LOG_ERROR("Action registry full (max %d)", MAX_ACTIONS);
    return false;
  }

  /* Register */
  actions[actions_allocated++] = action;

  LOG_DEBUG("Registered action: %s", action->name);

  return true;
}

bool
action_unregister(const char* name)
{
  if (!initialized) {
    LOG_ERROR("Action registry not initialized");
    return false;
  }

  if (name == NULL) {
    return false;
  }

  /* Find and remove */
  for (uint32_t i = 0; i < actions_allocated; i++) {
    if (strcmp(actions[i]->name, name) == 0) {
      LOG_DEBUG("Unregistered action: %s", name);

      /* Shift remaining actions */
      for (uint32_t j = i; j < actions_allocated - 1; j++) {
        actions[j] = actions[j + 1];
      }
      actions_allocated--;

      return true;
    }
  }

  return false;
}

Action*
action_lookup(const char* name)
{
  if (!initialized || name == NULL) {
    return NULL;
  }

  for (uint32_t i = 0; i < actions_allocated; i++) {
    if (strcmp(actions[i]->name, name) == 0) {
      return actions[i];
    }
  }

  return NULL;
}

Action**
action_get_all(void)
{
  if (!initialized) {
    return NULL;
  }

  /* Ensure NULL terminator */
  if (actions_allocated < MAX_ACTIONS) {
    actions[actions_allocated] = NULL;
  }

  return actions;
}

uint32_t
action_count(void)
{
  return actions_allocated;
}

bool
action_invoke(const char* name, ActionInvocation* inv)
{
  if (!initialized) {
    LOG_ERROR("Action registry not initialized");
    return false;
  }

  Action* action = action_lookup(name);
  if (action == NULL) {
    LOG_DEBUG("Action not found: %s", name);
    return false;
  }

  /* Resolve target */
  TargetID target = resolve_target(action, inv);

  /* Check if target is required */
  if (action->target_required && target == TARGET_ID_NONE) {
    LOG_DEBUG("Action '%s' requires target but none available", name);
    return false;
  }

  /* Build invocation context */
  ActionInvocation effective_inv;
  if (inv != NULL) {
    effective_inv = *inv;
  } else {
    memset(&effective_inv, 0, sizeof(effective_inv));
  }
  effective_inv.target = target;

  /* Call the callback */
  LOG_DEBUG("Invoking action: %s (target=%" PRIu64 ")", name, target);

  bool result = action->callback(&effective_inv);

  if (!result) {
    LOG_DEBUG("Action '%s' returned failure", name);
  }

  return result;
}

bool
action_exists(const char* name)
{
  return action_lookup(name) != NULL;
}