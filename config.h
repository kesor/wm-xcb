/*
 * config.h - Configuration system public interface
 *
 * Provides generic configuration for window manager settings.
 * Configuration is decoupled from component details - this module
 * does not know about specific actions, keybindings, or components.
 *
 * Usage:
 * - Include config.def.h for base configuration types
 * - Include config.wm.def.h for WM-specific wiring (keybindings, etc.)
 * - Call config_init() during WM startup to initialize wiring
 */

#ifndef CONFIG_H
#define CONFIG_H

/*
 * Initialize the configuration system
 * This sets up wiring for keybindings and other WM-specific configuration.
 * Called from keybinding component init.
 */
void config_init(void);

#endif /* CONFIG_H */