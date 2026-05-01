/*
 * keybinding.c - Keybinding component implementation
 *
 * Converts KEY_PRESS and KEY_RELEASE X events into action invocations.
 * Uses the action registry to look up and execute actions.
 *
 * This component:
 * - Registers XCB handlers for KEY_PRESS and KEY_RELEASE on init
 * - Looks up keybindings when a key event occurs
 * - Invokes actions from the action registry
 * - Unregisters XCB handlers on shutdown
 *
 * The action registry provides target resolution, so keybindings
 * don't need to know about the focus or monitor components.
 *
 * NOTE: This component SENDS requests but does NOT handle them.
 * It should NOT be registered as a handler for request types.
 */

#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>

#include "keybinding.h"
#include "src/actions/keybinding-binding.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"

/* Config system initialization */
extern void config_init(void);

/*
 * Action name constants for backward compatibility
 * These map enum values to action name strings.
 */
#define _ACTION_FOCUS_CURRENT   "focus.focus-current"
#define _ACTION_FOCUS_PREV      "focus.focus-prev"
#define _ACTION_TAG_VIEW        "tag-manager.view"
#define _ACTION_TAG_TOGGLE      "tag-manager.toggle"
#define _ACTION_TAG_CLIENT_MOVE "tag-manager.client-move"
#define _ACTION_FULLSCREEN      "fullscreen.toggle"
#define _ACTION_TILE_MONITOR    "monitor.tile"

/*
 * Internal state
 */
static bool initialized = false;

/*
 * Keybinding component
 *
 * Note: requests is NULL because this component only SENDS requests,
 * it does not handle them. The requests array in HubComponent is for
 * requests that THIS component handles, not requests it sends.
 */
HubComponent keybinding_component = {
  .name       = "keybinding",
  .requests   = NULL, /* We don't handle requests, we send them */
  .targets    = NULL, /* We don't adopt targets */
  .executor   = NULL, /* No executor - we only generate requests */
  .registered = false,
};

/*
 * Initialize the keybinding component
 */
void
keybinding_init(void)
{
  if (initialized) {
    return;
  }

  LOG_DEBUG("Initializing keybinding component");

  /* Register with hub */
  hub_register_component(&keybinding_component);

  /* Initialize action registry (if not already initialized) */
  action_registry_init();

  /* Initialize keybinding binding system */
  keybinding_binding_init();

  /* Initialize config (which wires keybindings) */
  config_init();

  /* Register XCB handlers for KEY_PRESS and KEY_RELEASE */
  xcb_handler_register(XCB_KEY_PRESS, &keybinding_component, keybinding_handle_key_press);
  xcb_handler_register(XCB_KEY_RELEASE, &keybinding_component, keybinding_handle_key_release);

  initialized = true;
  LOG_DEBUG("Keybinding component initialized with %" PRIu32 " bindings", num_keybindings);
}

/*
 * Shutdown the keybinding component
 */
void
keybinding_shutdown(void)
{
  if (!initialized) {
    return;
  }

  LOG_DEBUG("Shutting down keybinding component");

  /* Unregister XCB handlers */
  xcb_handler_unregister_component(&keybinding_component);

  /* Shutdown keybinding binding system */
  keybinding_binding_shutdown();

  /* Shutdown action registry */
  action_registry_shutdown();

  /* Unregister from hub */
  hub_unregister_component("keybinding");

  initialized = false;
  LOG_DEBUG("Keybinding component shutdown complete");
}

/*
 * Look up a keybinding by modifiers and keycode
 */
const KeyBinding*
keybinding_lookup(uint32_t modifiers, xcb_keycode_t keycode)
{
  return keybinding_binding_lookup(modifiers, keycode);
}

/*
 * Get the configured key bindings array
 */
const KeyBinding**
keybinding_get_bindings(void)
{
  return keybinding_binding_get_all();
}

/*
 * Get the number of configured key bindings
 */
uint32_t
keybinding_get_count(void)
{
  return keybinding_binding_get_count();
}

/*
 * Handle KEY_PRESS events
 * Called from XCB event loop via xcb_handler_dispatch()
 */
void
keybinding_handle_key_press(void* event)
{
  xcb_key_press_event_t* e = (xcb_key_press_event_t*) event;

  /* Log the key event for debugging */
  LOG_DEBUG("KEY_PRESS: state=0x%" PRIx32 " detail=%" PRIu8,
            e->state, e->detail);

  /* Look up and execute the keybinding */
  if (!keybinding_binding_execute(e->state, e->detail)) {
    LOG_DEBUG("Keybinding: no binding for key state=0x%" PRIx32 " keycode=%" PRIu8,
              e->state, e->detail);
  }
}

/*
 * Handle KEY_RELEASE events
 * Currently unused but available for future extension
 */
void
keybinding_handle_key_release(void* event)
{
  xcb_key_release_event_t* e = (xcb_key_release_event_t*) event;

  LOG_DEBUG("KEY_RELEASE: state=0x%" PRIx32 " detail=%" PRIu8,
            e->state, e->detail);

  /* For now, key releases don't trigger any actions.
   * In the future, we might want to track key hold duration
   * or implement key repeat behavior here. */
}

/*
 * Legacy support: convert KeybindingAction to action name string
 * This is used by tests that expect the old enum-based approach
 */
const char*
keybinding_action_to_name(KeybindingAction action)
{
  switch (action) {
  case KEYBINDING_ACTION_FOCUS_CLIENT:
    return _ACTION_FOCUS_CURRENT;
  case KEYBINDING_ACTION_FOCUS_PREV:
    return _ACTION_FOCUS_PREV;
  case KEYBINDING_ACTION_TAG_VIEW:
    return _ACTION_TAG_VIEW;
  case KEYBINDING_ACTION_TAG_TOGGLE:
    return _ACTION_TAG_TOGGLE;
  case KEYBINDING_ACTION_TAG_CLIENT_TOGGLE:
    return _ACTION_TAG_CLIENT_MOVE;
  case KEYBINDING_ACTION_TOGGLE_FULLSCREEN:
    return _ACTION_FULLSCREEN;
  case KEYBINDING_ACTION_TILE_MONITOR:
    return _ACTION_TILE_MONITOR;
  default:
    return NULL;
  }
}