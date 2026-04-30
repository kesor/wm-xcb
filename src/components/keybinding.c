/*
 * keybinding.c - Keybinding component implementation
 *
 * Converts KEY_PRESS and KEY_RELEASE X events into hub requests.
 * Key bindings are configurable via config.def.h.
 *
 * This component:
 * - Registers XCB handlers for KEY_PRESS and KEY_RELEASE on init
 * - Looks up keybindings from config when a key event occurs
 * - Sends appropriate requests to the hub based on the action
 * - Unregisters XCB handlers on shutdown
 *
 * The hub routes requests to the appropriate components (focus, tag, etc.)
 *
 * NOTE: This component SENDS requests but does NOT handle them.
 * It should NOT be registered as a handler for request types.
 */

#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "keybinding.h"
#include "src/components/focus.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-signals.h"
#include "wm-states.h"

/*
 * Active keybinding configuration
 *
 * Keep this configuration private to this translation unit so the
 * implementation owns the binding data instead of exporting globals
 * that appear externally overridable.
 */
static const KeyBinding default_keybindings[] = {
  /* Focus actions */
  /* Mod+Enter: focus current client */
  { XCB_MOD_MASK_4,                                       36, KEYBINDING_ACTION_FOCUS_CLIENT,      0 }, /* keycode 36 = Return */

  /* Mod+Shift+Enter: focus previous client */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  36, KEYBINDING_ACTION_FOCUS_PREV,        0 },

  /* Tag view actions - Mod+1 through Mod+9 */
  { XCB_MOD_MASK_4,                                       10, KEYBINDING_ACTION_TAG_VIEW,          1 }, /* keycode 10 = 1 */
  { XCB_MOD_MASK_4,                                       11, KEYBINDING_ACTION_TAG_VIEW,          2 }, /* keycode 11 = 2 */
  { XCB_MOD_MASK_4,                                       12, KEYBINDING_ACTION_TAG_VIEW,          3 }, /* keycode 12 = 3 */
  { XCB_MOD_MASK_4,                                       13, KEYBINDING_ACTION_TAG_VIEW,          4 }, /* keycode 13 = 4 */
  { XCB_MOD_MASK_4,                                       14, KEYBINDING_ACTION_TAG_VIEW,          5 }, /* keycode 14 = 5 */
  { XCB_MOD_MASK_4,                                       15, KEYBINDING_ACTION_TAG_VIEW,          6 }, /* keycode 15 = 6 */
  { XCB_MOD_MASK_4,                                       16, KEYBINDING_ACTION_TAG_VIEW,          7 }, /* keycode 16 = 7 */
  { XCB_MOD_MASK_4,                                       17, KEYBINDING_ACTION_TAG_VIEW,          8 }, /* keycode 17 = 8 */
  { XCB_MOD_MASK_4,                                       18, KEYBINDING_ACTION_TAG_VIEW,          9 }, /* keycode 18 = 9 */

  /* Tag toggle actions - Mod+Shift+1 through Mod+Shift+9 */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  10, KEYBINDING_ACTION_TAG_TOGGLE,        1 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  11, KEYBINDING_ACTION_TAG_TOGGLE,        2 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  12, KEYBINDING_ACTION_TAG_TOGGLE,        3 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  13, KEYBINDING_ACTION_TAG_TOGGLE,        4 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  14, KEYBINDING_ACTION_TAG_TOGGLE,        5 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  15, KEYBINDING_ACTION_TAG_TOGGLE,        6 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  16, KEYBINDING_ACTION_TAG_TOGGLE,        7 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  17, KEYBINDING_ACTION_TAG_TOGGLE,        8 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  18, KEYBINDING_ACTION_TAG_TOGGLE,        9 },

  /* Tag client toggle - Mod+Shift+Alt+1 through Mod+Shift+Alt+9 (move focused client to tag) */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 10, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 1 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 11, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 2 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 12, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 3 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 13, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 4 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 14, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 5 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 15, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 6 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 16, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 7 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 17, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 8 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 18, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 9 },

  /* Fullscreen toggle - Mod+f (keycode 41 = f) */
  { XCB_MOD_MASK_4,                                       41, KEYBINDING_ACTION_TOGGLE_FULLSCREEN, 0 },

  /* Tile toggle - Mod+i (keycode 31 = i) */
  { XCB_MOD_MASK_4,                                       31, KEYBINDING_ACTION_TILE_MONITOR,      0 },
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
 * External dependencies - resolved at runtime
 */

/* Current focused client - provided by focus component (stub for now) */
__attribute__((weak)) TargetID
focus_get_current_client(void)
{
  /* TODO: Implement focus component integration */
  /* For now, return NONE - focus component will provide this */
  return TARGET_ID_NONE;
}

/* Current selected monitor - provided by monitor system */
extern Monitor* monitor_get_selected(void);
extern TargetID monitor_get_current_monitor(void);

/*
 * Convert KeybindingAction to RequestType
 * These request types match what the focus and tag components expect
 */
static RequestType
action_to_request_type(KeybindingAction action)
{
  switch (action) {
  case KEYBINDING_ACTION_FOCUS_CLIENT:
    return REQ_KEYBINDING_FOCUS;      /* = 1 = REQ_CLIENT_FOCUS */
  case KEYBINDING_ACTION_FOCUS_PREV:
    return REQ_KEYBINDING_FOCUS_PREV; /* = 3 = REQ_CLIENT_FOCUS_PREV */
  case KEYBINDING_ACTION_FOCUS_NEXT:
    return REQ_KEYBINDING_FOCUS;      /* Use same as current for now */
  case KEYBINDING_ACTION_TAG_VIEW:
    return REQ_KEYBINDING_TAG_VIEW;   /* = 4 = REQ_MONITOR_TAG_VIEW */
  case KEYBINDING_ACTION_TAG_TOGGLE:
    return REQ_KEYBINDING_TAG_TOGGLE; /* = 5 = REQ_MONITOR_TAG_TOGGLE */
  case KEYBINDING_ACTION_TAG_CLIENT_TOGGLE:
    return REQ_TAG_CLIENT_TOGGLE;     /* Move client to tag */
  case KEYBINDING_ACTION_CLOSE_CLIENT:
    return REQ_KEYBINDING_CLOSE;      /* = 6 = REQ_CLIENT_CLOSE */
  case KEYBINDING_ACTION_TOGGLE_FULLSCREEN:
    return REQ_CLIENT_FULLSCREEN;     /* Fullscreen request */
  default:
    return 0;
  }
}

/*
 * Get the current target for a request type
 */
static TargetID
get_target_for_request(RequestType type)
{
  /* For focus and close requests, use current client */
  if (type == REQ_KEYBINDING_FOCUS ||
      type == REQ_KEYBINDING_FOCUS_PREV ||
      type == REQ_KEYBINDING_CLOSE) {
    return focus_get_current_client();
  }

  /* For tag requests, use current monitor */
  if (type == REQ_KEYBINDING_TAG_VIEW ||
      type == REQ_KEYBINDING_TAG_TOGGLE) {
    return monitor_get_current_monitor();
  }

  return TARGET_ID_NONE;
}

/*
 * Log a debug message for focus request failures
 */
static const char*
focus_action_name(RequestType type)
{
  switch (type) {
  case REQ_KEYBINDING_FOCUS:
    return "focus current client";
  case REQ_KEYBINDING_FOCUS_PREV:
    return "focus previous client";
  default:
    return "focus";
  }
}

/*
 * Send a focus request to the hub
 */
static void
send_focus_request(RequestType type)
{
  TargetID target = get_target_for_request(type);
  if (target != TARGET_ID_NONE) {
    hub_send_request(type, target);
    LOG_DEBUG("Keybinding sent focus request type=%" PRIu32 " target=%" PRIu64,
              type, target);
  } else {
    LOG_DEBUG("Keybinding: no focused client to %s", focus_action_name(type));
  }
}

/*
 * Send a tag view/toggle request to the hub
 */
static void
send_tag_request(RequestType type, uint32_t tag)
{
  TargetID target = get_target_for_request(type);
  if (target != TARGET_ID_NONE) {
    hub_send_request_data(type, target, &tag);
    LOG_DEBUG("Keybinding sent tag request type=%" PRIu32 " tag=%" PRIu32, type, tag);
  } else {
    LOG_DEBUG("Keybinding: no selected monitor for tag request");
  }
}

/*
 * Execute a keybinding action
 */
static void
execute_keybinding(const KeyBinding* binding)
{
  RequestType req_type = action_to_request_type(binding->action);

  switch (binding->action) {
  case KEYBINDING_ACTION_FOCUS_CLIENT:
  case KEYBINDING_ACTION_FOCUS_PREV:
  case KEYBINDING_ACTION_FOCUS_NEXT:
    send_focus_request(req_type);
    break;

  case KEYBINDING_ACTION_TAG_VIEW:
  case KEYBINDING_ACTION_TAG_TOGGLE:
    send_tag_request(req_type, binding->arg);
    break;

  case KEYBINDING_ACTION_TAG_CLIENT_TOGGLE:
    /* Move focused client to/from tag - sends to current client */
    {
      Client* c = focus_get_focused_client();
      if (c != NULL) {
        uint32_t tag_data = binding->arg;
        hub_send_request_data(req_type, c->target.id, &tag_data);
        LOG_DEBUG("Keybinding sent client tag toggle type=%" PRIu32 " target=%" PRIu64 " tag=%" PRIu32,
                  req_type, (uint64_t) c->target.id, tag_data);
      } else {
        LOG_DEBUG("Keybinding: no focused client for tag client toggle");
      }
    }
    break;

  case KEYBINDING_ACTION_CLOSE_CLIENT:
    send_focus_request(REQ_KEYBINDING_CLOSE);
    break;

  case KEYBINDING_ACTION_TOGGLE_FULLSCREEN:
    send_focus_request(REQ_CLIENT_FULLSCREEN);
    break;

  case KEYBINDING_ACTION_TILE_MONITOR: {
    TargetID target = monitor_get_current_monitor();
    if (target != TARGET_ID_NONE) {
      hub_send_request(REQ_MONITOR_TILE, target);
      LOG_DEBUG("Keybinding sent tile request to monitor target=%" PRIu64, target);
    } else {
      LOG_DEBUG("Keybinding: no selected monitor for tile request");
    }
  } break;


  default:
    LOG_DEBUG("Keybinding: unhandled action %d", binding->action);
    break;
  }
}

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

  /* Register XCB handlers for KEY_PRESS and KEY_RELEASE */
  xcb_handler_register(XCB_KEY_PRESS, &keybinding_component, keybinding_handle_key_press);
  xcb_handler_register(XCB_KEY_RELEASE, &keybinding_component, keybinding_handle_key_release);

  initialized = true;
  LOG_DEBUG("Keybinding component initialized");
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

    /* Match modifiers exactly (after normalizing) */
    if (binding->modifiers != normalized_mods) {
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

  LOG_DEBUG("Keybinding matched: action=%d arg=%" PRIu32,
            binding->action, binding->arg);

  /* Execute the binding action */
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