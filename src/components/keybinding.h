/*
 * keybinding.h - Keybinding component for the window manager
 *
 * Converts KEY_PRESS and KEY_RELEASE X events into hub requests.
 * Key bindings are configurable via config.def.h.
 *
 * Component lifecycle:
 * - on_init(): register XCB handlers for KEY_PRESS and KEY_RELEASE
 * - on_shutdown(): unregister XCB handlers
 * - No target adoption needed (works with global target resolution)
 *
 * Request types sent:
 * - REQ_CLIENT_FOCUS: Mod+Enter focuses the current client
 * - REQ_CLIENT_FOCUS_PREV: Mod+Shift+Enter focuses previous client
 * - REQ_MONITOR_TAG_VIEW: Mod+1-9 switches to tag view (1-indexed)
 * - REQ_MONITOR_TAG_TOGGLE: Mod+Shift+1-9 toggles tag visibility
 *
 * Note: This component sends requests but does NOT handle them.
 * It should not be registered as a handler for these request types.
 */

#ifndef WM_KEYBINDING_H_
#define WM_KEYBINDING_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "wm-hub.h"

/*
 * Request type constants
 * These match the request types expected by focus and tag components.
 * The keybinding component uses these to send requests, not handle them.
 */
enum KeybindingRequestType {
  REQ_KEYBINDING_FOCUS      = 1, /* Maps to REQ_CLIENT_FOCUS */
  REQ_KEYBINDING_FOCUS_PREV = 3, /* Maps to REQ_CLIENT_FOCUS_PREV */
  REQ_KEYBINDING_TAG_VIEW   = 4, /* Maps to REQ_MONITOR_TAG_VIEW */
  REQ_KEYBINDING_TAG_TOGGLE = 5, /* Maps to REQ_MONITOR_TAG_TOGGLE */
  REQ_KEYBINDING_CLOSE      = 6, /* Maps to REQ_CLIENT_CLOSE */
};

/*
 * Key binding action types
 * Used internally to classify what action a keybinding should trigger
 */
typedef enum {
  KEYBINDING_ACTION_FOCUS_CLIENT,      /* Focus current client */
  KEYBINDING_ACTION_FOCUS_PREV,        /* Focus previous client */
  KEYBINDING_ACTION_FOCUS_NEXT,        /* Focus next client */
  KEYBINDING_ACTION_TAG_VIEW,          /* View specific tag (1-9) */
  KEYBINDING_ACTION_TAG_TOGGLE,        /* Toggle tag visibility */
  KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, /* Move current client to tag */
  KEYBINDING_ACTION_CLOSE_CLIENT,      /* Close current client */
  KEYBINDING_ACTION_TOGGLE_FULLSCREEN, /* Toggle fullscreen */
  KEYBINDING_ACTION_TILE_MONITOR,      /* Tile all clients on current monitor */
  KEYBINDING_ACTION_LAST,
} KeybindingAction;

/*
 * Key binding configuration entry
 */
typedef struct KeyBinding {
  uint32_t         modifiers; /* XCB modifier mask (Shift, Control, Mod1, etc.) */
  xcb_keycode_t    keycode;   /* X11 keycode */
  KeybindingAction action;    /* Action to perform */
  uint32_t         arg;       /* Action argument (tag number for tag actions) */
} KeyBinding;

/*
 * Symbolic target IDs for keybinding resolution
 * These special values are resolved at runtime to actual targets
 */
enum KeybindingTarget {
  TARGET_CURRENT_CLIENT  = 1, /* Currently focused client */
  TARGET_CURRENT_MONITOR = 2, /* Currently selected monitor */
  TARGET_SELECTION_FIRST = 3, /* First client in selection order */
  TARGET_SELECTION_LAST  = 4, /* Last client in selection order */
};

/*
 * Number of configurable key bindings
 */
#define KEYBINDING_MAX_BINDINGS 32

/*
 * Number of available tags (standard dwm uses 9)
 */
#define KEYBINDING_NUM_TAGS     9

/*
 * Common XCB modifier mask values
 * These are XCB standard values for modifier keys
 */
/* ShiftMask = 1 << 0 = 0x0001 */
/* LockMask = 1 << 1 = 0x0002 */
/* ControlMask = 1 << 2 = 0x0004 */
/* Mod1Mask = 1 << 3 = 0x0008 (Alt key) */
/* Mod2Mask = 1 << 4 = 0x0010 (NumLock) */
/* Mod3Mask = 1 << 5 = 0x0020 */
/* Mod4Mask = 1 << 6 = 0x0040 (Super/Windows key) */
/* Mod5Mask = 1 << 7 = 0x0080 */

/*
 * Common modifier combinations
 * These can be OR'd together for common combinations
 */
#define KEYBINDING_MOD_ANY      0        /* Any modifiers (for testing) */
#define KEYBINDING_MOD_SHIFT    (1 << 0) /* Shift key */
#define KEYBINDING_MOD_CONTROL  (1 << 2) /* Control key */
#define KEYBINDING_MOD_MOD1     (1 << 3) /* Alt key */
#define KEYBINDING_MOD_MOD4     (1 << 6) /* Super/Windows key */

/*
 * Keybinding component structure
 * Registered with the hub to handle event routing
 *
 * Note: requests is NULL because this component only SENDS requests,
 * it does not handle them. The requests array in HubComponent is for
 * requests that THIS component handles, not requests it sends.
 */
extern HubComponent keybinding_component;

/*
 * Keybinding handler functions
 * Called by XCB event loop via xcb_handler_dispatch()
 */

/*
 * Handle KEY_PRESS events
 * Looks up the keybinding in config, then sends the appropriate hub request
 */
void keybinding_handle_key_press(void* event);

/*
 * Handle KEY_RELEASE events
 * Currently unused but available for key release handling
 */
void keybinding_handle_key_release(void* event);

/*
 * Convert X event to key binding lookup
 * Returns the matching KeyBinding or NULL if not found
 */
const KeyBinding* keybinding_lookup(uint32_t modifiers, xcb_keycode_t keycode);

/*
 * Get the configured key bindings array
 */
const KeyBinding* keybinding_get_bindings(void);

/*
 * Get the number of configured key bindings
 */
uint32_t keybinding_get_count(void);

/*
 * Initialize the keybinding component
 * Called by hub during component registration
 */
void keybinding_init(void);

/*
 * Shutdown the keybinding component
 * Called during cleanup
 */
void keybinding_shutdown(void);

#endif /* WM_KEYBINDING_H_ */