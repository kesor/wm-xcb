#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>
#include "wm-states.h"

#include <X11/keysymdef.h>

/*
 * ============================================================================
 * STATE MACHINE TRANSITIONS (existing config)
 * ============================================================================
 *
 * State machine transitions for mouse/keyboard interactions.
 * This defines how the window manager reacts to user input.
 */

static struct StateTransition transitions[] = {
  /* hover window type , combination of buttons and keys , event type , state to move from , state to move into */
  { WndTypeClient, EventTypeBtnPress,   KeyShift, 1, 0,    STATE_IDLE,          STATE_WINDOW_MOVE   },
  { WndTypeClient, EventTypeBtnRelease, KeyNone,  1, 0,    STATE_WINDOW_MOVE,   STATE_IDLE          },

  { WndTypeClient, EventTypeBtnPress,   KeyShift, 3, 0,    STATE_IDLE,          STATE_WINDOW_RESIZE },
  { WndTypeClient, EventTypeBtnRelease, 0,        3, 0,    STATE_WINDOW_RESIZE, STATE_IDLE          },

  { WndTypeRoot,   EventTypeBtnPress,   KeyMod1,  1, 0,    STATE_IDLE,          STATE_WINDOW_MOVE   },
  { WndTypeRoot,   EventTypeKeyPress,   KeyShift, 0, XK_A, STATE_IDLE,          STATE_IDLE          },
};

/*
 * ============================================================================
 * KEYBINDING CONFIGURATION (new)
 * ============================================================================
 *
 * Keybindings are defined as an array of KeyBinding structures.
 * Each binding specifies:
 * - modifiers: XCB modifier mask (XCB_MOD_MASK_*)
 * - keycode: X11 keycode
 * - action: Action to perform (see KeyBindingAction enum)
 * - arg: Argument for the action (e.g., tag number for tag actions)
 */

/*
 * Key binding action types
 * Used internally to classify what action a keybinding should trigger.
 */
typedef enum KeyBindingAction {
  KEYBINDING_ACTION_NONE              = 0,  /* No action */
  KEYBINDING_ACTION_FOCUS_CLIENT      = 1,  /* Focus current client */
  KEYBINDING_ACTION_FOCUS_PREV        = 2,  /* Focus previous client */
  KEYBINDING_ACTION_FOCUS_NEXT        = 3,  /* Focus next client */
  KEYBINDING_ACTION_TAG_VIEW          = 4,  /* View specific tag (arg = tag 1-9) */
  KEYBINDING_ACTION_TAG_TOGGLE        = 5,  /* Toggle tag visibility (arg = tag 1-9) */
  KEYBINDING_ACTION_TAG_CLIENT_MOVE   = 6,  /* Move client to tag (arg = tag 1-9) */
  KEYBINDING_ACTION_CLOSE_CLIENT      = 7,  /* Close current client */
  KEYBINDING_ACTION_TOGGLE_FULLSCREEN = 8,  /* Toggle fullscreen */
  KEYBINDING_ACTION_TILE_MONITOR      = 9,  /* Tile all clients on monitor */
  KEYBINDING_ACTION_SPAWN_TERMINAL    = 11, /* Spawn terminal */
  KEYBINDING_ACTION_LAST,
} KeyBindingAction;

/*
 * Key binding configuration entry
 */
typedef struct KeyBinding {
  uint32_t         modifiers; /* XCB modifier mask */
  uint8_t          keycode;   /* X11 keycode */
  KeyBindingAction action;    /* Action to perform */
  uint32_t         arg;       /* Action argument */
} KeyBinding;

/*
 * Default keybindings
 * These follow dwm conventions using Mod4 (Super/Windows key)
 */
static const KeyBinding default_keybindings[] = {
  /* Focus actions */
  { XCB_MOD_MASK_4,                                       36, KEYBINDING_ACTION_FOCUS_CLIENT,      0 }, /* Mod+Enter */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  36, KEYBINDING_ACTION_FOCUS_PREV,        0 }, /* Mod+Shift+Enter */

  /* Tag view actions - Mod+1 through Mod+9 */
  { XCB_MOD_MASK_4,                                       10, KEYBINDING_ACTION_TAG_VIEW,          1 },
  { XCB_MOD_MASK_4,                                       11, KEYBINDING_ACTION_TAG_VIEW,          2 },
  { XCB_MOD_MASK_4,                                       12, KEYBINDING_ACTION_TAG_VIEW,          3 },
  { XCB_MOD_MASK_4,                                       13, KEYBINDING_ACTION_TAG_VIEW,          4 },
  { XCB_MOD_MASK_4,                                       14, KEYBINDING_ACTION_TAG_VIEW,          5 },
  { XCB_MOD_MASK_4,                                       15, KEYBINDING_ACTION_TAG_VIEW,          6 },
  { XCB_MOD_MASK_4,                                       16, KEYBINDING_ACTION_TAG_VIEW,          7 },
  { XCB_MOD_MASK_4,                                       17, KEYBINDING_ACTION_TAG_VIEW,          8 },
  { XCB_MOD_MASK_4,                                       18, KEYBINDING_ACTION_TAG_VIEW,          9 },

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

  /* Move client to tag - Mod+Shift+Alt+1 through Mod+Shift+Alt+9 */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 10, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   1 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 11, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   2 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 12, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   3 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 13, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   4 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 14, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   5 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 15, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   6 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 16, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   7 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 17, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   8 },
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 18, KEYBINDING_ACTION_TAG_CLIENT_MOVE,   9 },

  /* Fullscreen toggle - Mod+f */
  { XCB_MOD_MASK_4,                                       41, KEYBINDING_ACTION_TOGGLE_FULLSCREEN, 0 },

  /* Tile monitor - Mod+Shift+BackSpace */
  { XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4,                  22, KEYBINDING_ACTION_TILE_MONITOR,      0 },
};

#define DEFAULT_KEYBINDING_COUNT (sizeof(default_keybindings) / sizeof(default_keybindings[0]))

/*
 * ============================================================================
 * WINDOW RULES CONFIGURATION (new)
 * ============================================================================
 */

/*
 * Floating rule - windows that should start floating
 */
typedef struct FloatRule {
  const char* class_name;      /* WM_CLASS class */
  const char* instance_name;   /* WM_CLASS instance */
  const char* title_substring; /* Match in window title */
} FloatRule;

/*
 * Tag rule - windows that should have specific tags
 */
typedef struct TagRule {
  const char* class_name;      /* WM_CLASS class */
  const char* instance_name;   /* WM_CLASS instance */
  const char* title_substring; /* Match in window title */
  uint32_t    tags;            /* Tag mask (1 << (tag - 1)) */
} TagRule;

/*
 * Default float rules
 */
static const FloatRule default_float_rules[] = {
  { .class_name = "steam", .instance_name = NULL, .title_substring = NULL             },
  { .class_name = NULL,    .instance_name = NULL, .title_substring = " - Preferences" },
  { .class_name = NULL,    .instance_name = NULL, .title_substring = " - Settings"    },
};

#define DEFAULT_FLOAT_RULE_COUNT (sizeof(default_float_rules) / sizeof(default_float_rules[0]))

/*
 * Default tag rules
 */
static const TagRule default_tag_rules[] = {
  { .class_name = "firefox",  .instance_name = NULL, .title_substring = NULL, .tags = 1 << 1 },
  { .class_name = "chromium", .instance_name = NULL, .title_substring = NULL, .tags = 1 << 1 },
  { .class_name = "slack",    .instance_name = NULL, .title_substring = NULL, .tags = 1 << 2 },
};

#define DEFAULT_TAG_RULE_COUNT (sizeof(default_tag_rules) / sizeof(default_tag_rules[0]))

/*
 * ============================================================================
 * COMPONENT PROPERTIES CONFIGURATION (new)
 * ============================================================================
 */

/*
 * Tag configuration
 */
enum {
  CONFIG_TAG_NUM_TAGS     = 9, /* Number of available tags (1-9) */
  CONFIG_TAG_DEFAULT_MASK = 1, /* Default tag for new clients (tag 1 = bit 0) */
};

/*
 * Gap configuration
 */
typedef struct GapConfig {
  int top;
  int bottom;
  int left;
  int right;
  int inner;
} GapConfig;

static const GapConfig default_gaps = {
  .top    = 0,
  .bottom = 0,
  .left   = 0,
  .right  = 0,
  .inner  = 10,
};

/*
 * Border configuration
 */
typedef struct BorderConfig {
  uint32_t color_normal;
  uint32_t color_focus;
  uint32_t color_urgent;
  uint32_t width;
} BorderConfig;

static const BorderConfig default_borders = {
  .color_normal = 0x333333,
  .color_focus  = 0x005577,
  .color_urgent = 0xff6600,
  .width        = 1,
};

/*
 * ============================================================================
 * CONFIGURATION API DECLARATIONS
 * ============================================================================
 */

const KeyBinding*   config_get_keybindings(void);
uint32_t            config_get_keybinding_count(void);
const FloatRule*    config_get_float_rules(void);
uint32_t            config_get_float_rule_count(void);
const TagRule*      config_get_tag_rules(void);
uint32_t            config_get_tag_rule_count(void);
const GapConfig*    config_get_gaps(void);
const BorderConfig* config_get_borders(void);
uint32_t            config_get_tag_count(void);

bool     config_should_float(const char* class_name, const char* instance_name, const char* title);
uint32_t config_get_tags(const char* class_name, const char* instance_name, const char* title);

#endif /* _CONFIG_H_ */