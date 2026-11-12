/*
 * keybinding-binding.c - Keybinding binding storage and lookup implementation
 *
 * This module manages a runtime binding table that maps key combinations
 * to action names. The bindings can be registered at runtime.
 */

#include "src/actions/keybinding-binding.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/actions/action-registry.h"
#include "src/core/wm-log.h"

/*
 * Keybinding storage - extra slot for NULL terminator
 */
static const KeyBinding* bindings[KEYBINDING_MAX_BINDINGS + 1];
static uint32_t          binding_count = 0;
static bool              initialized   = false;

/*
 * Modifier bits a key press may carry that the binding did not ask for:
 * CapsLock is always Lock, and NumLock is Mod2 on a standard XKB keymap. A
 * server that reports a different NumLock bit overrides that half through
 * keybinding_binding_set_ignored_modifiers().
 */
#define KEYBINDING_DEFAULT_IGNORED_MODIFIERS (XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)

static uint32_t ignored_modifiers = KEYBINDING_DEFAULT_IGNORED_MODIFIERS;

/*
 * Internal: find binding index by modifiers and keycode
 */
static int32_t
find_binding_index(uint32_t modifiers, xcb_keycode_t keycode)
{
  /* Normalize modifiers */
  uint32_t normalized = modifiers & ~ignored_modifiers;

  for (uint32_t i = 0; i < binding_count; i++) {
    const KeyBinding* b = bindings[i];
    if (b == NULL)
      continue;

    /* Normalize binding's modifiers */
    uint32_t binding_normalized = b->modifiers & ~ignored_modifiers;

    if (binding_normalized == normalized && b->keycode == keycode) {
      return (int32_t) i;
    }
  }

  return -1;
}

/*
 * Internal: resolve target based on action's target type
 */
static TargetID
resolve_binding_target(Action* action)
{
  if (action == NULL) {
    return TARGET_ID_NONE;
  }

  if (action->target_resolver != NULL) {
    return action->target_resolver(NULL);
  }

  switch (action->target_type) {
  case ACTION_TARGET_CLIENT:
    return action_resolve_current_client();
  case ACTION_TARGET_MONITOR:
    return action_resolve_current_monitor();
  case ACTION_TARGET_CURRENT:
    return action_resolve_current_client();
  case ACTION_TARGET_BOUND:
  case ACTION_TARGET_ANY:
  case ACTION_TARGET_NONE:
  default:
    return TARGET_ID_NONE;
  }
}

void
keybinding_binding_init(void)
{
  if (initialized) {
    LOG_WARN("Keybinding binding system already initialized");
    return;
  }

  LOG_DEBUG("Initializing keybinding binding system");
  memset(bindings, 0, sizeof(bindings));
  binding_count     = 0;
  ignored_modifiers = KEYBINDING_DEFAULT_IGNORED_MODIFIERS;
  initialized       = true;
}

void
keybinding_binding_set_ignored_modifiers(uint32_t mask)
{
  ignored_modifiers = mask;
}

void
keybinding_binding_shutdown(void)
{
  if (!initialized) {
    return;
  }

  LOG_DEBUG("Shutting down keybinding binding system");

  keybinding_binding_clear();
  initialized = false;
}

void
keybinding_binding_clear(void)
{
  if (!initialized) {
    return;
  }

  for (uint32_t i = 0; i < binding_count; i++)
    free((void*) bindings[i]);

  memset(bindings, 0, sizeof(bindings));
  binding_count = 0;
}

/*
 * The one place a binding enters the table, so the two entry points below
 * cannot disagree about what a refusal means.
 */
static KeybindingRegisterResult
register_binding(uint32_t      modifiers,
                 xcb_keycode_t keycode,
                 const char*   action,
                 uint32_t      arg,
                 bool          has_arg)
{
  if (!initialized) {
    LOG_ERROR("Keybinding binding system not initialized");
    return KEYBINDING_REFUSED;
  }

  if (action == NULL || action[0] == '\0') {
    LOG_ERROR("Cannot register binding with empty action");
    return KEYBINDING_REFUSED;
  }

  if (find_binding_index(modifiers, keycode) >= 0)
    return KEYBINDING_ALREADY_BOUND;

  if (binding_count >= KEYBINDING_MAX_BINDINGS)
    return KEYBINDING_TABLE_FULL;

  /* Allocate and store binding */
  KeyBinding* b = malloc(sizeof(KeyBinding));
  if (b == NULL) {
    LOG_ERROR("Failed to allocate keybinding");
    return KEYBINDING_REFUSED;
  }

  b->modifiers = modifiers;
  b->keycode   = keycode;
  b->action    = action;
  b->arg       = arg;
  b->userdata  = NULL;
  b->has_arg   = has_arg;

  bindings[binding_count++] = b;

  if (has_arg) {
    LOG_DEBUG("Registered keybinding: mod=0x%x keycode=%d action=%s arg=%" PRIu32,
              modifiers, keycode, action, arg);
  } else {
    LOG_DEBUG("Registered keybinding: mod=0x%x keycode=%d action=%s",
              modifiers, keycode, action);
  }

  return KEYBINDING_REGISTERED;
}

KeybindingRegisterResult
keybinding_binding_register(uint32_t modifiers, xcb_keycode_t keycode, const char* action)
{
  return register_binding(modifiers, keycode, action, 0, false);
}

KeybindingRegisterResult
keybinding_binding_register_with_arg(uint32_t      modifiers,
                                     xcb_keycode_t keycode,
                                     const char*   action,
                                     uint32_t      arg)
{
  return register_binding(modifiers, keycode, action, arg, true);
}


bool
keybinding_binding_unregister(uint32_t modifiers, xcb_keycode_t keycode)
{
  if (!initialized) {
    LOG_ERROR("Keybinding binding system not initialized");
    return false;
  }

  int32_t idx = find_binding_index(modifiers, keycode);
  if (idx < 0) {
    return false;
  }

  /* Free the binding */
  free((void*) bindings[idx]);

  /* Shift remaining bindings */
  for (uint32_t i = (uint32_t) idx; i < binding_count - 1; i++) {
    bindings[i] = bindings[i + 1];
  }
  binding_count--;

  return true;
}

const KeyBinding*
keybinding_binding_lookup(uint32_t modifiers, xcb_keycode_t keycode)
{
  if (!initialized) {
    return NULL;
  }

  int32_t idx = find_binding_index(modifiers, keycode);
  if (idx < 0) {
    return NULL;
  }

  return bindings[idx];
}

KeyBindingLookup
keybinding_binding_lookup_with_invocation(uint32_t modifiers, xcb_keycode_t keycode)
{
  KeyBindingLookup result = {
    .binding = NULL,
    .inv     = { 0 },
  };

  if (!initialized) {
    return result;
  }

  const KeyBinding* binding = keybinding_binding_lookup(modifiers, keycode);
  if (binding == NULL) {
    return result;
  }

  result.binding = binding;

  /* Look up action for target resolution */
  Action* action = action_lookup(binding->action);
  if (action != NULL) {
    result.inv.target = resolve_binding_target(action);
  } else {
    result.inv.target = TARGET_ID_NONE;
  }

  /* Set data pointer to arg if present */
  if (binding->has_arg) {
    /* Intentional int-to-pointer cast for passing tag numbers */
    result.inv.data     = (void*) (uintptr_t) binding->arg;    // NOLINT
    result.inv.userdata = binding->userdata;
  } else {
    result.inv.data     = binding->userdata;
    result.inv.userdata = binding->userdata;
  }

  return result;
}

const KeyBinding* const*
keybinding_binding_get_all(void)
{
  if (!initialized) {
    return NULL;
  }

  /* Ensure NULL terminator (array has extra slot for this) */
  bindings[binding_count] = NULL;

  return bindings;
}

uint32_t
keybinding_binding_get_count(void)
{
  return binding_count;
}

bool
keybinding_binding_invoke(const KeyBinding* binding)
{
  if (!initialized || binding == NULL) {
    return false;
  }

  /* Look up action */
  Action* action = action_lookup(binding->action);
  if (action == NULL) {
    LOG_DEBUG("Action not found for binding: %s", binding->action);
    return false;
  }

  /* Build invocation context */
  ActionInvocation inv = {
    .target         = resolve_binding_target(action),
    .correlation_id = 0,
    .userdata       = binding->userdata,
  };

  /* Set data pointer */
  if (binding->has_arg) {
    /* Intentional int-to-pointer cast for passing tag numbers */
    inv.data = (void*) (uintptr_t) binding->arg;    // NOLINT
  } else {
    inv.data = binding->userdata;
  }

  /* Check target requirement */
  if (action->target_required && inv.target == TARGET_ID_NONE) {
    LOG_DEBUG("Action '%s' requires target but none available", binding->action);
    return false;
  }

  /* Invoke action */
  return action_invoke(binding->action, &inv);
}

bool
keybinding_binding_execute(uint32_t modifiers, xcb_keycode_t keycode)
{
  const KeyBinding* binding = keybinding_binding_lookup(modifiers, keycode);
  if (binding == NULL) {
    return false;
  }

  return keybinding_binding_invoke(binding);
}