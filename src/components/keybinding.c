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
#include <xcb/xcb_keysyms.h>

#include "keybinding.h"
#include "src/actions/action-registry.h"
#include "src/actions/keybinding-binding.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-signals.h"
#include "wm-states.h"

/*
 * Action names for keybindings
 * These are the names registered with the action registry.
 */
#define ACTION_FOCUS_CURRENT   "focus.focus-current"
#define ACTION_FOCUS_PREV      "focus.focus-prev"
#define ACTION_TAG_VIEW        "tag-manager.view"
#define ACTION_TAG_TOGGLE      "tag-manager.toggle"
#define ACTION_TAG_CLIENT_MOVE "tag-manager.client-move"
#define ACTION_FULLSCREEN      "fullscreen.toggle"
#define ACTION_TILE_MONITOR    "monitor.tile"

/*
 * Default keybindings using action names
 *
 * These bind key combinations to action names (strings).
 * The action registry resolves targets at invocation time.
 */
static const KeyBinding default_keybindings[] = {
  /* Focus actions */
  /* Mod+Enter: focus current client */
  { XCB_MOD_MASK_4,                                       36, ACTION_FOCUS_CURRENT,   0, NULL, false }, /* keycode 36 = Return */

  /* Mod+Shift+Enter: focus previous client */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  36, ACTION_FOCUS_PREV,      0, NULL, false },

  /* Tag view actions - Mod+1 through Mod+9 */
  { XCB_MOD_MASK_4,                                       10, ACTION_TAG_VIEW,        1, NULL, true  }, /* keycode 10 = 1 */
  { XCB_MOD_MASK_4,                                       11, ACTION_TAG_VIEW,        2, NULL, true  }, /* keycode 11 = 2 */
  { XCB_MOD_MASK_4,                                       12, ACTION_TAG_VIEW,        3, NULL, true  }, /* keycode 12 = 3 */
  { XCB_MOD_MASK_4,                                       13, ACTION_TAG_VIEW,        4, NULL, true  }, /* keycode 13 = 4 */
  { XCB_MOD_MASK_4,                                       14, ACTION_TAG_VIEW,        5, NULL, true  }, /* keycode 14 = 5 */
  { XCB_MOD_MASK_4,                                       15, ACTION_TAG_VIEW,        6, NULL, true  }, /* keycode 15 = 6 */
  { XCB_MOD_MASK_4,                                       16, ACTION_TAG_VIEW,        7, NULL, true  }, /* keycode 16 = 7 */
  { XCB_MOD_MASK_4,                                       17, ACTION_TAG_VIEW,        8, NULL, true  }, /* keycode 17 = 8 */
  { XCB_MOD_MASK_4,                                       18, ACTION_TAG_VIEW,        9, NULL, true  }, /* keycode 18 = 9 */

  /* Tag toggle actions - Mod+Shift+1 through Mod+Shift+9 */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  10, ACTION_TAG_TOGGLE,      1, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  11, ACTION_TAG_TOGGLE,      2, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  12, ACTION_TAG_TOGGLE,      3, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  13, ACTION_TAG_TOGGLE,      4, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  14, ACTION_TAG_TOGGLE,      5, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  15, ACTION_TAG_TOGGLE,      6, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  16, ACTION_TAG_TOGGLE,      7, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  17, ACTION_TAG_TOGGLE,      8, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  18, ACTION_TAG_TOGGLE,      9, NULL, true  },

  /* Tag client toggle - Mod+Shift+Alt+1 through Mod+Shift+Alt+9 (move focused client to tag) */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 10, ACTION_TAG_CLIENT_MOVE, 1, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 11, ACTION_TAG_CLIENT_MOVE, 2, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 12, ACTION_TAG_CLIENT_MOVE, 3, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 13, ACTION_TAG_CLIENT_MOVE, 4, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 14, ACTION_TAG_CLIENT_MOVE, 5, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 15, ACTION_TAG_CLIENT_MOVE, 6, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 16, ACTION_TAG_CLIENT_MOVE, 7, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 17, ACTION_TAG_CLIENT_MOVE, 8, NULL, true  },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 18, ACTION_TAG_CLIENT_MOVE, 9, NULL, true  },

  /* Fullscreen toggle - Mod+f (keycode 41 = f) */
  { XCB_MOD_MASK_4,                                       41, ACTION_FULLSCREEN,      0, NULL, false }, /* keycode 41 = f */

  /* Tile toggle - Mod+i (keycode 31 = i) */
  { XCB_MOD_MASK_4,                                       31, ACTION_TILE_MONITOR,    0, NULL, false }, /* keycode 31 = i */
};

static const KeyBinding* keybindings     = default_keybindings;
static uint32_t          num_keybindings = sizeof(default_keybindings) / sizeof(default_keybindings[0]);

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

  /* Register default keybindings */
  for (uint32_t i = 0; i < num_keybindings; i++) {
    const KeyBinding* kb = &keybindings[i];
    if (kb->has_arg) {
      keybinding_binding_register_with_arg(kb->modifiers, kb->keycode, kb->action, kb->arg);
    } else {
      keybinding_binding_register(kb->modifiers, kb->keycode, kb->action);
    }
  }

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
  /* Normalize modifiers: ignore lock and mode switches for matching */
  uint32_t normalized_mods = modifiers & ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2);

  for (uint32_t i = 0; i < num_keybindings; i++) {
    const KeyBinding* binding = &keybindings[i];

    /* Match keycode */
    if (binding->keycode != 0 && binding->keycode != keycode) {
      continue;
    }

    /* Normalize binding's modifiers for comparison */
    uint32_t binding_normalized = binding->modifiers & ~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2);

    /* Match modifiers exactly (after normalizing) */
    if (binding_normalized != normalized_mods) {
      continue;
    }

    return binding;
  }

  return NULL;
}

/*
 * Get the configured key bindings array
 */
const KeyBinding*
keybinding_get_bindings(void)
{
  return keybindings;
}

/*
 * Get the number of configured key bindings
 */
uint32_t
keybinding_get_count(void)
{
  return num_keybindings;
}

/*
 * Execute a keybinding action
 */
static void
execute_keybinding(const KeyBinding* binding)
{
  if (binding == NULL || binding->action == NULL) {
    return;
  }

  /* Look up the action */
  Action* action = action_lookup(binding->action);
  if (action == NULL) {
    LOG_DEBUG("Keybinding: action not found: %s", binding->action);
    return;
  }

  /* Build invocation context */
  ActionInvocation inv = {
    .target         = TARGET_ID_NONE,
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

  /* Resolve target based on action's target_type */
  if (action->target_type == ACTION_TARGET_CLIENT) {
    inv.target = action_resolve_current_client();
  } else if (action->target_type == ACTION_TARGET_MONITOR) {
    inv.target = action_resolve_current_monitor();
  }

  /* Check if target is required */
  if (action->target_required && inv.target == TARGET_ID_NONE) {
    LOG_DEBUG("Keybinding: action '%s' requires target but none available", binding->action);
    return;
  }

  /* Invoke the action */
  LOG_DEBUG("Keybinding invoking action: %s (target=%" PRIu64 ")", binding->action, inv.target);

  bool result = action->callback(&inv);
  if (!result) {
    LOG_DEBUG("Keybinding: action '%s' returned failure", binding->action);
  }
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

  /* Look up the keybinding */
  const KeyBinding* binding = keybinding_lookup(e->state, e->detail);
  if (binding == NULL) {
    /* Unknown key - do nothing */
    LOG_DEBUG("Keybinding: no binding for key state=0x%" PRIx32 " keycode=%" PRIu8,
              e->state, e->detail);
    return;
  }

  LOG_DEBUG("Keybinding matched: action=%s arg=%" PRIu32,
            binding->action, binding->arg);

  /* Execute the binding action via action registry */
  execute_keybinding(binding);
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
    return ACTION_FOCUS_CURRENT;
  case KEYBINDING_ACTION_FOCUS_PREV:
    return ACTION_FOCUS_PREV;
  case KEYBINDING_ACTION_TAG_VIEW:
    return ACTION_TAG_VIEW;
  case KEYBINDING_ACTION_TAG_TOGGLE:
    return ACTION_TAG_TOGGLE;
  case KEYBINDING_ACTION_TAG_CLIENT_TOGGLE:
    return ACTION_TAG_CLIENT_MOVE;
  case KEYBINDING_ACTION_TOGGLE_FULLSCREEN:
    return ACTION_FULLSCREEN;
  case KEYBINDING_ACTION_TILE_MONITOR:
    return ACTION_TILE_MONITOR;
  default:
    return NULL;
  }
}