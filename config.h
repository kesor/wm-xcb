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
 * - Call config_load_runtime() from main, before the components start, to read
 *   the user's config file
 * - Call config_init() during WM startup to initialize wiring
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/*
 * Read the user's configuration file.
 *
 * A path of NULL reads $WM_CONFIG, else $XDG_CONFIG_HOME/wm/config, else
 * $HOME/.config/wm/config. A file that is not there is not an error: the
 * compiled configuration (config.def.h, config.wm.def.h) then applies.
 *
 * Every rejected line is reported once, as ERROR, with its line number, and
 * skipped. The rest of the file still applies.
 *
 * Returns true when a file was read and kept. Call this before the components
 * start: config_init() wires the bindings it found, and nothing can change
 * them afterwards until the WM restarts.
 */
bool config_load_runtime(const char* path);

/*
 * Initialize the configuration system.
 * This wires keybindings from the config file, or from config.wm.def.h when the
 * file named no binding.
 */
void config_init(void);

/*
 * Free the configuration file read by config_load_runtime().
 *
 * Call this after keybinding_shutdown(): the binding table holds pointers into
 * the file's binding and action names, so freeing it first would leave the
 * table pointing at freed memory.
 */
void config_shutdown(void);

/*
 * The name the config file gave tag `index` (0-based), or NULL when the file
 * named that tag nothing. tag_list_init() gives the name to the tag it creates,
 * and tag_get_name() returns "1" to "9" for the ones left unnamed.
 */
const char* config_get_tag_name(int index);

#endif /* CONFIG_H */
