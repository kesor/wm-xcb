/*
 * keybindings.h - User-configurable keybindings for the window manager
 *
 * This file contains the keybinding configuration that can be customized by users.
 * Copy this file to keybindings.h and modify the keybindings array.
 *
 * Default keybindings are provided below - modify as needed.
 *
 * Format: { modifiers, keycode, action, argument }
 * - modifiers: XCB modifier mask (e.g., MODKEY, MODKEY | SHIFT, etc.)
 * - keycode: X11 keycode (use xev or xmodmap -pk to find keycodes)
 * - action: One of the KEYBINDING_ACTION_* values
 * - argument: Additional argument for the action (tag number, etc.)
 */

#ifndef WM_KEYBINDINGS_H_
#define WM_KEYBINDINGS_H_

/*
 * Common modifier definitions
 */
#define MODKEY XCB_MOD_MASK_4              /* Super/Windows key */
#define SHIFT XCB_MOD_MASK_SHIFT
#define CONTROL XCB_MOD_MASK_CONTROL
#define ALT XCB_MOD_MASK_MOD_1
#define MOD5 XCB_MOD_MASK_5

/*
 * Number of tags (standard dwm uses 9)
 */
#define NUM_TAGS 9

/*
 * Keybinding definitions
 *
 * Users can customize these bindings by modifying the keybindings array below.
 * The terminator entry { 0, 0, 0, 0 } marks the end of the array.
 */
static const KeyBinding keybindings[] = {
  /* Focus actions */
  { MODKEY,                    36, KEYBINDING_ACTION_FOCUS_CLIENT,      0 }, /* Mod+Return: focus current client */
  { MODKEY | SHIFT,           36, KEYBINDING_ACTION_FOCUS_PREV,        0 }, /* Mod+Shift+Return: focus previous client */

  /* Tag view - Mod+1 through Mod+9 */
  { MODKEY,                    10, KEYBINDING_ACTION_TAG_VIEW,          1 },
  { MODKEY,                    11, KEYBINDING_ACTION_TAG_VIEW,          2 },
  { MODKEY,                    12, KEYBINDING_ACTION_TAG_VIEW,          3 },
  { MODKEY,                    13, KEYBINDING_ACTION_TAG_VIEW,          4 },
  { MODKEY,                    14, KEYBINDING_ACTION_TAG_VIEW,          5 },
  { MODKEY,                    15, KEYBINDING_ACTION_TAG_VIEW,          6 },
  { MODKEY,                    16, KEYBINDING_ACTION_TAG_VIEW,          7 },
  { MODKEY,                    17, KEYBINDING_ACTION_TAG_VIEW,          8 },
  { MODKEY,                    18, KEYBINDING_ACTION_TAG_VIEW,          9 },

  /* Tag toggle - Mod+Shift+1 through Mod+Shift+9 */
  { MODKEY | SHIFT,           10, KEYBINDING_ACTION_TAG_TOGGLE,        1 },
  { MODKEY | SHIFT,           11, KEYBINDING_ACTION_TAG_TOGGLE,        2 },
  { MODKEY | SHIFT,           12, KEYBINDING_ACTION_TAG_TOGGLE,        3 },
  { MODKEY | SHIFT,           13, KEYBINDING_ACTION_TAG_TOGGLE,        4 },
  { MODKEY | SHIFT,           14, KEYBINDING_ACTION_TAG_TOGGLE,        5 },
  { MODKEY | SHIFT,           15, KEYBINDING_ACTION_TAG_TOGGLE,        6 },
  { MODKEY | SHIFT,           16, KEYBINDING_ACTION_TAG_TOGGLE,        7 },
  { MODKEY | SHIFT,           17, KEYBINDING_ACTION_TAG_TOGGLE,        8 },
  { MODKEY | SHIFT,           18, KEYBINDING_ACTION_TAG_TOGGLE,        9 },

  /* Tag client toggle - Mod+Shift+Alt+1 through Mod+Shift+Alt+9 */
  { MODKEY | SHIFT | MOD5,    10, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 1 },
  { MODKEY | SHIFT | MOD5,    11, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 2 },
  { MODKEY | SHIFT | MOD5,    12, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 3 },
  { MODKEY | SHIFT | MOD5,    13, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 4 },
  { MODKEY | SHIFT | MOD5,    14, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 5 },
  { MODKEY | SHIFT | MOD5,    15, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 6 },
  { MODKEY | SHIFT | MOD5,    16, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 7 },
  { MODKEY | SHIFT | MOD5,    17, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 8 },
  { MODKEY | SHIFT | MOD5,    18, KEYBINDING_ACTION_TAG_CLIENT_TOGGLE, 9 },

  /* Fullscreen toggle - Mod+f (keycode 41 = f) */
  { MODKEY,                    41, KEYBINDING_ACTION_TOGGLE_FULLSCREEN, 0 },

  /* Tile toggle - Mod+i (keycode 31 = i) */
  { MODKEY,                    31, KEYBINDING_ACTION_TILE_MONITOR,      0 },

  /* Terminator - do not remove */
  { 0, 0, 0, 0 },
};

#endif /* WM_KEYBINDINGS_H_ */