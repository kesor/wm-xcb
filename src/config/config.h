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

#include "src/event-types.h"

/*
 * Emitted after a SIGHUP reread the config file and wired what it found. A
 * component that caches a value read from the configuration rebuilds it here.
 * A reload the parser rejected emits nothing: the running configuration is
 * still the one the components already know.
 */
enum ConfigEvents {
  EVT_CONFIG_RELOADED = EVT_CONFIG_BASE + 0,
};

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
 * Reread the file the WM started with, after a SIGHUP.
 *
 * The parser runs on the whole file before anything changes. A file with any
 * rejected line leaves the running configuration in place and logs one error:
 * a typo must not take away the keys the user is typing with. A clean file, or
 * no file at all, replaces the bindings, the float rules and the tag names,
 * then emits EVT_CONFIG_RELOADED. Clients, their tags, focus and geometry are
 * not touched.
 *
 * Returns true when the file was applied. Call it from the event loop, after
 * the components are up: the keybinding table has to exist before it is rebuilt.
 */
bool config_reload(void);

/*
 * Initialize the configuration system.
 * This wires keybindings from the config file, or from config.wm.def.h when the
 * file named no binding.
 *
 * Returns how many of the file's bind lines reached the binding table. Zero
 * means the compiled bindings are what is in the table: the file named none, or
 * none of the ones it named could be used.
 */
uint32_t config_init(void);

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
 * named that tag nothing. The tag-manager component creates the tags and gives
 * each the name this answers, and a reload renames them; tag_get_name() returns
 * "1" to "9" for the ones left unnamed.
 */
const char* config_get_tag_name(int index);

/*
 * Whether a window matches a float rule, so it starts out of the layout.
 *
 * `class_name` and `instance_name` are the two strings of the window's
 * WM_CLASS, and `title` is WM_NAME; any of them may be NULL, which is a window
 * that never set that property. The answer comes from the file's `float`
 * lines, or from the compiled rules when it named none.
 */
bool config_should_float(const char* class_name, const char* instance_name, const char* title);

#endif /* CONFIG_H */
