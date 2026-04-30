/*
 * keybinding-binding.h - Keybinding binding storage and lookup
 *
 * Manages the keybinding table that maps key events to action names.
 * Keybindings are defined in config.def.h and looked up at runtime.
 *
 * This separates binding storage from action invocation:
 * - config.def.h wires key combos to action names
 * - keybinding component looks up action name on KEY_PRESS
 * - action-registry handles target resolution and invocation
 *
 * Note: KeyBinding struct is defined in keybinding.h
 */

#ifndef _WM_KEYBINDING_BINDING_H_
#define _WM_KEYBINDING_BINDING_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

/* Include keybinding.h for KeyBinding definition */
#include "src/components/keybinding.h"

/* Include action registry for ActionInvocation definition */
#include "action-registry.h"

/*
 * Binding lookup result
 */
typedef struct KeyBindingLookup {
  const KeyBinding* binding; /* The matching binding, or NULL */
  ActionInvocation  inv;     /* Invocation context with resolved target */
} KeyBindingLookup;

/*
 * Keybinding Binding API
 */

/*
 * Initialize the keybinding binding system.
 */
void keybinding_binding_init(void);

/*
 * Shutdown the keybinding binding system.
 */
void keybinding_binding_shutdown(void);

/*
 * Register a keybinding with an action.
 *
 * @param modifiers  XCB modifier mask (e.g., XCB_MOD_MASK_4)
 * @param keycode    X11 keycode (e.g., 41 for 'f')
 * @param action     Action name to invoke (e.g., "fullscreen.toggle")
 * @return           true on success, false on failure
 */
bool keybinding_binding_register(uint32_t modifiers, xcb_keycode_t keycode, const char* action);

/*
 * Register a keybinding with an action and integer argument.
 *
 * @param modifiers  XCB modifier mask
 * @param keycode    X11 keycode
 * @param action     Action name to invoke
 * @param arg        Integer argument to pass to action
 * @return           true on success, false on failure
 */
bool keybinding_binding_register_with_arg(uint32_t      modifiers,
                                          xcb_keycode_t keycode,
                                          const char*   action,
                                          uint32_t      arg);

/*
 * Unregister a keybinding by modifiers and keycode.
 * Returns true if found and removed, false if not found.
 */
bool keybinding_binding_unregister(uint32_t modifiers, xcb_keycode_t keycode);

/*
 * Look up a keybinding by modifiers and keycode.
 *
 * @param modifiers  XCB modifier mask
 * @param keycode    X11 keycode
 * @return           Matching KeyBinding or NULL if not found
 */
const KeyBinding* keybinding_binding_lookup(uint32_t modifiers, xcb_keycode_t keycode);

/*
 * Look up a keybinding and prepare invocation context.
 *
 * @param modifiers  XCB modifier mask
 * @param keycode    X11 keycode
 * @return           KeyBindingLookup with binding and resolved invocation
 */
KeyBindingLookup keybinding_binding_lookup_with_invocation(uint32_t modifiers, xcb_keycode_t keycode);

/*
 * Get all registered bindings.
 * Returns a NULL-terminated array of KeyBinding pointers.
 */
const KeyBinding** keybinding_binding_get_all(void);

/*
 * Get the number of registered bindings.
 */
uint32_t keybinding_binding_get_count(void);

/*
 * Invoke the action for a keybinding.
 *
 * @param binding  The binding to invoke
 * @return         true if action was found and invoked successfully
 */
bool keybinding_binding_invoke(const KeyBinding* binding);

/*
 * Execute a keybinding by lookup and invocation.
 * Combines lookup and invoke for convenience.
 *
 * @param modifiers  XCB modifier mask
 * @param keycode    X11 keycode
 * @return           true if a binding was found and invoked successfully
 */
bool keybinding_binding_execute(uint32_t modifiers, xcb_keycode_t keycode);

#endif /* _WM_KEYBINDING_BINDING_H_ */