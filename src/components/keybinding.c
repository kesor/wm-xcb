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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#include "config.h"
#include "keybinding.h"
#include "src/actions/keybinding-binding.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-xcb.h"

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
  .name                  = KEYBINDING_COMPONENT_NAME,
  .requests              = NULL, /* We don't handle requests, we send them */
  .accepted_target_names = NULL, /* We don't adopt targets */
  .accepted_targets      = NULL,
  .executor              = NULL, /* No executor - we only generate requests */
  .registered            = false,
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

  /* The grabs need dpy and root from setup_xcb(), and the bindings wired just
   * above: main() sets up X before it starts the components. */
  keybinding_grab_keys();

  /* Register XCB handlers for KEY_PRESS and KEY_RELEASE */
  xcb_handler_register(XCB_KEY_PRESS, &keybinding_component, keybinding_handle_key_press);
  xcb_handler_register(XCB_KEY_RELEASE, &keybinding_component, keybinding_handle_key_release);
  xcb_handler_register(XCB_MAPPING_NOTIFY, &keybinding_component, keybinding_handle_mapping_notify);

  initialized = true;
  LOG_DEBUG("Keybinding component initialized with %" PRIu32 " bindings",
            keybinding_binding_get_count());
}

/*
 * Check if the component is initialized (for the startup check)
 */
bool
keybinding_is_initialized(void)
{
  return initialized;
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
  hub_unregister_component(KEYBINDING_COMPONENT_NAME);

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
 * The modifier bit the server uses for Num_Lock, read from the modifier map at
 * grab time. Zero when there is no connection or the keymap has no Num_Lock.
 */
static uint32_t
keybinding_numlock_mask(void)
{
  if (dpy == NULL)
    return 0;

  /* allow-blocking: startup only, the event loop has not started yet */
  xcb_key_symbols_t* symbols = xcb_key_symbols_alloc(dpy);
  if (symbols == NULL)
    return 0;

  xcb_keycode_t* keycodes = xcb_key_symbols_get_keycode(symbols, XK_Num_Lock);
  xcb_key_symbols_free(symbols);

  if (keycodes == NULL)
    return 0;

  uint32_t mask = 0;

  if (keycodes[0] != XCB_NO_SYMBOL) {
    /* allow-blocking: startup only, the event loop has not started yet */
    xcb_get_modifier_mapping_reply_t* reply =
        xcb_get_modifier_mapping_reply(dpy, xcb_get_modifier_mapping(dpy), NULL);

    if (reply != NULL) {
      const xcb_keycode_t* map     = xcb_get_modifier_mapping_keycodes(reply);
      int                  per_mod = reply->keycodes_per_modifier;

      for (int mod = 0; mod < 8 && mask == 0; mod++) {
        for (int i = 0; i < per_mod; i++) {
          if (map[mod * per_mod + i] == keycodes[0]) {
            mask = 1u << mod;
            break;
          }
        }
      }

      free(reply);
    }
  }

  free(keycodes);
  return mask;
}

/*
 * Grab every configured binding on the root window.
 *
 * Without the grab the server sends the press to the focused client and no
 * KEY_PRESS reaches the WM. A press made with CapsLock or NumLock on carries
 * their modifier bit in `state`, and a grab only matches the exact mask, so
 * each binding is grabbed with the four combinations of those two bits.
 *
 * Called again on MappingNotify: a keymap change can move a keycode, and the
 * grab is per keycode. The NumLock bit is not re-read there because asking the
 * server for the modifier map needs a reply, which the event loop may not wait
 * for; the modifier Num_Lock sits on does not change when a key moves.
 */
void
keybinding_grab_keys(void)
{
  if (dpy == NULL || root == XCB_NONE)
    return;

  uint32_t numlock = keybinding_numlock_mask();
  if (numlock != 0)
    keybinding_binding_set_ignored_modifiers(XCB_MOD_MASK_LOCK | numlock);

  /* Release this client's grabs on the root window first, so a re-grab after a
   * remap replaces them instead of relying on the server to treat a repeated
   * grab as the same one. */
  xcb_ungrab_key(dpy, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);

  const uint32_t lock_combinations[] = {
    0, XCB_MOD_MASK_LOCK, numlock, numlock | XCB_MOD_MASK_LOCK
  };
  const KeyBinding* const* bindings = keybinding_binding_get_all();
  uint32_t                 grabbed  = 0;

  for (uint32_t i = 0; bindings != NULL && bindings[i] != NULL; i++) {
    for (size_t m = 0; m < sizeof(lock_combinations) / sizeof(lock_combinations[0]); m++) {
      xcb_grab_key(dpy,
                   1,
                   root,
                   (uint16_t) (bindings[i]->modifiers | lock_combinations[m]),
                   bindings[i]->keycode,
                   XCB_GRAB_MODE_ASYNC,
                   XCB_GRAB_MODE_ASYNC);
      grabbed++;
    }
  }

  LOG_DEBUG("Grabbed %" PRIu32 " key combinations on the root window", grabbed);
}

/*
 * Get the configured key bindings array
 * Returns: pointer to internal array (do not modify)
 */
const KeyBinding* const*
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
 * Handle MAPPING_NOTIFY events.
 *
 * A keyboard or modifier remap can move a keycode onto a different key, and
 * the grabs are per keycode, so they have to be made again for the bindings to
 * keep matching the keys the user pressed. A pointer remap does not touch keys.
 */
void
keybinding_handle_mapping_notify(void* event)
{
  xcb_mapping_notify_event_t* e = (xcb_mapping_notify_event_t*) event;

  if (e->request != XCB_MAPPING_KEYBOARD && e->request != XCB_MAPPING_MODIFIER)
    return;

  LOG_DEBUG("MAPPING_NOTIFY: request=%" PRIu8 ", re-grabbing keys", e->request);
  keybinding_grab_keys();
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