/*
 * config.def.h - Default configuration for the window manager
 *
 * This is the dwm-style configuration file. Users customize this file
 * and recompile the window manager.
 *
 * This file:
 * 1. Includes the base config (config.def.h) for rules, gaps, borders
 * 2. Registers keybindings via keybinding_binding_register()
 *
 * The config system is generic - it doesn't know about specific components.
 * Keybindings are wired here using action names from the action registry.
 */

#ifndef CONFIG_WM_H
#define CONFIG_WM_H

/*
 * Base configuration types
 * wm-states.h defines StateTransition used in config.def.h
 */
#include "wm-states.h"

/*
 * Keybinding registration API
 * The keybinding component provides runtime binding storage.
 */
#include "src/actions/keybinding-binding.h"
#include "src/components/keybinding.h"

/*
 * Common modifier shortcuts
 */
#define MODKEY XCB_MOD_MASK_4 /* Super/Windows key */

/*
 * ============================================================================
 * KEYBINDING WIRING
 * ============================================================================
 *
 * Keybindings are registered at runtime using action names.
 * Each binding maps a key combination to an action from the action registry.
 *
 * Action names follow the pattern: "component.action"
 * Available actions are registered by components during their init.
 */

/*
 * Helper macros for cleaner keybinding registration
 */

/* Basic keybinding: Mod+<key> → action */
#define KB(mod, keycode, action)                    \
  keybinding_binding_register(mod, keycode, action)

/* Keybinding with argument: Mod+Shift+<num> → action(arg) */
#define KB_TAG(mod, keycode, action, tag)                         \
  keybinding_binding_register_with_arg(mod, keycode, action, tag)

/*
 * Default keybindings
 * These follow dwm conventions using Mod4 (Super/Windows key)
 */
static void
config_wire_keybindings(void)
{
  /* Focus actions */
  KB(MODKEY, 36, "focus.focus-current");                   /* Mod+Enter: focus current client */
  KB(MODKEY | XCB_MOD_MASK_SHIFT, 36, "focus.focus-prev"); /* Mod+Shift+Enter: focus previous */

  /* Tag view actions - Mod+1 through Mod+9 */
  KB_TAG(MODKEY, 10, "tag-manager.view", 1);
  KB_TAG(MODKEY, 11, "tag-manager.view", 2);
  KB_TAG(MODKEY, 12, "tag-manager.view", 3);
  KB_TAG(MODKEY, 13, "tag-manager.view", 4);
  KB_TAG(MODKEY, 14, "tag-manager.view", 5);
  KB_TAG(MODKEY, 15, "tag-manager.view", 6);
  KB_TAG(MODKEY, 16, "tag-manager.view", 7);
  KB_TAG(MODKEY, 17, "tag-manager.view", 8);
  KB_TAG(MODKEY, 18, "tag-manager.view", 9);

  /* Tag toggle actions - Mod+Shift+1 through Mod+Shift+9 */
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 10, "tag-manager.toggle", 1);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 11, "tag-manager.toggle", 2);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 12, "tag-manager.toggle", 3);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 13, "tag-manager.toggle", 4);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 14, "tag-manager.toggle", 5);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 15, "tag-manager.toggle", 6);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 16, "tag-manager.toggle", 7);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 17, "tag-manager.toggle", 8);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT, 18, "tag-manager.toggle", 9);

  /* Move client to tag - Mod+Shift+Alt+1 through Mod+Shift+Alt+9 */
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 10, "tag-manager.client-move", 1);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 11, "tag-manager.client-move", 2);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 12, "tag-manager.client-move", 3);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 13, "tag-manager.client-move", 4);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 14, "tag-manager.client-move", 5);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 15, "tag-manager.client-move", 6);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 16, "tag-manager.client-move", 7);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 17, "tag-manager.client-move", 8);
  KB_TAG(MODKEY | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5, 18, "tag-manager.client-move", 9);

  /* Fullscreen toggle - Mod+f */
  KB(MODKEY, 41, "fullscreen.toggle");

  /* Tile monitor - Mod+i */
  KB(MODKEY, 31, "monitor.tile");
}

#endif /* CONFIG_WM_H */