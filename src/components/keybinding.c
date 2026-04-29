/*
 * keybinding.c - Keybinding component implementation
 *
 * Converts KEY_PRESS and KEY_RELEASE X events into hub requests.
 * Key bindings are configurable via the keybindings array.
 *
 * This component:
 * - Registers XCB handlers for KEY_PRESS and KEY_RELEASE on init
 * - Looks up keybindings from config when a key event occurs
 * - Sends appropriate requests to the hub based on the action
 * - Unregisters XCB handlers on shutdown
 *
 * The hub routes requests to the appropriate components (focus, tag, etc.)
 */

#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "keybinding.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-signals.h"
#include "wm-states.h"
#include "src/target/monitor.h"

/*
 * Request type constants for keybinding-generated requests
 * These are the request types sent to the hub when keybindings fire
 *
 * Note: These are in the 100-199 range to avoid conflicts with
 * other component request types (focus=1, fullscreen=1,2, etc.)
 */
enum KeybindingRequestType {
  REQ_KEYBINDING_FOCUS        = 100,
  REQ_KEYBINDING_FOCUS_PREV   = 101,
  REQ_KEYBINDING_FOCUS_NEXT   = 102,
  REQ_KEYBINDING_TAG_VIEW     = 103,
  REQ_KEYBINDING_TAG_TOGGLE   = 104,
  REQ_KEYBINDING_CLOSE        = 105,
};

/*
 * Key request data structure
 * Passed to hub for keybinding requests
 */
typedef struct {
  union {
    uint32_t tag;           /* For tag requests: 1-indexed tag number */
    uint32_t direction;     /* For focus requests: 0=next, 1=prev */
  };
  void* reserved;           /* For future extension */
} KeybindingRequestData;

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
 * Default keybindings
 * Format: { modifiers, keycode, action, arg }
 *
 * Default bindings use Mod4 (Super/Windows key) as the modifier.
 * Keycodes are looked up at runtime from keysyms.
 *
 * Default bindings:
 *   Mod+Enter       -> Focus current client
 *   Mod+Shift+Enter -> Focus previous client
 *   Mod+1..9        -> View tags 1..9
 *   Mod+Shift+1..9  -> Toggle tags 1..9
 */
static const KeyBinding default_keybindings[] = {
  /* Focus actions */
  /* Mod+Enter: focus current client */
  { XCB_MOD_MASK_4, 36, KEYBINDING_ACTION_FOCUS_CLIENT, 0 },   /* keycode 36 = Return */

  /* Mod+Shift+Enter: focus previous client */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 36, KEYBINDING_ACTION_FOCUS_PREV, 0 },

  /* Tag view actions - Mod+1 through Mod+9 */
  { XCB_MOD_MASK_4, 10, KEYBINDING_ACTION_TAG_VIEW, 1 },   /* keycode 10 = 1 */
  { XCB_MOD_MASK_4, 11, KEYBINDING_ACTION_TAG_VIEW, 2 },  /* keycode 11 = 2 */
  { XCB_MOD_MASK_4, 12, KEYBINDING_ACTION_TAG_VIEW, 3 },   /* keycode 12 = 3 */
  { XCB_MOD_MASK_4, 13, KEYBINDING_ACTION_TAG_VIEW, 4 },   /* keycode 13 = 4 */
  { XCB_MOD_MASK_4, 14, KEYBINDING_ACTION_TAG_VIEW, 5 },   /* keycode 14 = 5 */
  { XCB_MOD_MASK_4, 15, KEYBINDING_ACTION_TAG_VIEW, 6 },   /* keycode 15 = 6 */
  { XCB_MOD_MASK_4, 16, KEYBINDING_ACTION_TAG_VIEW, 7 },   /* keycode 16 = 7 */
  { XCB_MOD_MASK_4, 17, KEYBINDING_ACTION_TAG_VIEW, 8 },   /* keycode 17 = 8 */
  { XCB_MOD_MASK_4, 18, KEYBINDING_ACTION_TAG_VIEW, 9 },   /* keycode 18 = 9 */

  /* Tag toggle actions - Mod+Shift+1 through Mod+Shift+9 */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 10, KEYBINDING_ACTION_TAG_TOGGLE, 1 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 11, KEYBINDING_ACTION_TAG_TOGGLE, 2 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 12, KEYBINDING_ACTION_TAG_TOGGLE, 3 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 13, KEYBINDING_ACTION_TAG_TOGGLE, 4 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 14, KEYBINDING_ACTION_TAG_TOGGLE, 5 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 15, KEYBINDING_ACTION_TAG_TOGGLE, 6 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 16, KEYBINDING_ACTION_TAG_TOGGLE, 7 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 17, KEYBINDING_ACTION_TAG_TOGGLE, 8 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 18, KEYBINDING_ACTION_TAG_TOGGLE, 9 },
};

/*
 * Keybindings array - points to config or defaults
 */
const KeyBinding* keybindings = default_keybindings;
const uint32_t   num_keybindings = sizeof(default_keybindings) / sizeof(default_keybindings[0]);

/*
 * Internal state
 */
static bool initialized = false;

/*
 * Keybinding request types that this component can send
 * 0-terminated array as required by HubComponent
 */
static const RequestType keybinding_requests[] = {
  REQ_KEYBINDING_FOCUS,
  REQ_KEYBINDING_FOCUS_PREV,
  REQ_KEYBINDING_FOCUS_NEXT,
  REQ_KEYBINDING_TAG_VIEW,
  REQ_KEYBINDING_TAG_TOGGLE,
  REQ_KEYBINDING_CLOSE,
  0,  /* Terminator */
};

/*
 * Target types this component works with (for future extension)
 * Currently keybindings work with both clients and monitors
 */
static const TargetType keybinding_targets[] = {
  TARGET_TYPE_CLIENT,
  TARGET_TYPE_MONITOR,
  TARGET_TYPE_NONE,  /* Terminator */
};

/*
 * Request executor for keybinding requests
 * Currently unused - keybindings generate requests but don't handle them
 */
static void
keybinding_executor(struct HubRequest* req)
{
  /* Keybinding component generates requests, it doesn't handle them.
   * This executor would only be called if something sends a request
   * directly to the keybinding component (which shouldn't happen). */
  LOG_DEBUG("keybinding_executor called with request type %" PRIu32, req->type);
}

/*
 * Keybinding component
 * Registered with hub to participate in the component ecosystem
 */
HubComponent keybinding_component = {
  .name       = "keybinding",
  .requests   = keybinding_requests,
  .targets    = keybinding_targets,
  .executor   = keybinding_executor,
  .registered = false,
};

/*
 * Convert KeybindingAction to RequestType
 */
static RequestType
action_to_request_type(KeybindingAction action)
{
  switch (action) {
    case KEYBINDING_ACTION_FOCUS_CLIENT:
      return REQ_KEYBINDING_FOCUS;
    case KEYBINDING_ACTION_FOCUS_PREV:
      return REQ_KEYBINDING_FOCUS_PREV;
    case KEYBINDING_ACTION_FOCUS_NEXT:
      return REQ_KEYBINDING_FOCUS_NEXT;
    case KEYBINDING_ACTION_TAG_VIEW:
      return REQ_KEYBINDING_TAG_VIEW;
    case KEYBINDING_ACTION_TAG_TOGGLE:
      return REQ_KEYBINDING_TAG_TOGGLE;
    case KEYBINDING_ACTION_CLOSE_CLIENT:
      return REQ_KEYBINDING_CLOSE;
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
      type == REQ_KEYBINDING_FOCUS_NEXT ||
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
    LOG_DEBUG("Keybinding: no focused client to %s",
              type == REQ_KEYBINDING_FOCUS_PREV ? "focus prev" : "focus next");
  }
}

/*
 * Send a tag view/toggle request to the hub
 */
static void
send_tag_request(RequestType type, uint32_t tag)
{
  TargetID              target = get_target_for_request(type);
  KeybindingRequestData data   = { .tag = tag };

  if (target != TARGET_ID_NONE) {
    hub_send_request_data(type, target, &data);
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

    case KEYBINDING_ACTION_CLOSE_CLIENT:
      send_focus_request(REQ_KEYBINDING_CLOSE);
      break;

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