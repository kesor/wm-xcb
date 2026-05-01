#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "wm-states.h"

/* Forward declarations */
typedef struct KeyBinding KeyBinding;

/*
 * Configuration API
 *
 * The config system provides read-only access to configuration values.
 * Configuration values are defined in config.def.h and accessed via
 * config_get_*() functions.
 *
 * Keybindings are NOT accessed here - they're registered directly
 * via keybinding_binding_register() in config.wm.def.h, keeping the
 * config system generic and decoupled from the keybinding component.
 */

/*
 * ============================================================================
 * STATE MACHINE TRANSITIONS
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
 * WINDOW RULES CONFIGURATION
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
 * COMPONENT PROPERTIES CONFIGURATION
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