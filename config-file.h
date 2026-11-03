/*
 * config-file.h - the user's configuration file
 *
 * The file holds three kinds of line:
 *
 *   # a comment
 *   bind Mod+1        tag-manager.view 1
 *   bind Mod+Return   terminal.spawn
 *   float class=Pavucontrol
 *   float title=" - Preferences"
 *   tags  1 2 3 4 dev chat
 *
 * This module only parses. config.c maps what it found onto the keybinding
 * table and the accessors the rest of the WM reads. Nothing here touches X, so
 * the format and its errors are testable without a display.
 *
 * A line that cannot be understood is recorded with its line number and
 * skipped; the rest of the file still applies. One typo then costs one
 * binding, not the whole configuration. config_load_runtime() in config.c is
 * what reports the recorded errors, so a caller that reads the file by hand
 * has to do the same.
 */

#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Every fixed-size table below bounds what a file may contain, so an error
 * says which limit was reached instead of dropping entries in silence. */
#define CONFIG_FILE_MAX_BINDS       64
#define CONFIG_FILE_MAX_FLOAT_RULES 16
#define CONFIG_FILE_MAX_TAGS        9
#define CONFIG_FILE_MAX_ERRORS      16
#define CONFIG_FILE_KEY_NAME_MAX    32
#define CONFIG_FILE_ACTION_NAME_MAX 64
#define CONFIG_FILE_ERROR_TEXT_MAX  160

/*
 * One `bind` line: the chord, the action it invokes, and the optional integer
 * argument the action receives (`tag-manager.view 2`).
 */
typedef struct ConfigBind {
  char     key[CONFIG_FILE_KEY_NAME_MAX]; /* the key alone, without modifiers */
  char     action[CONFIG_FILE_ACTION_NAME_MAX];
  uint32_t modifiers;                     /* XCB mask the chord asks for */
  uint32_t keysym;                        /* the key as a keysym, 0 if unknown */
  uint32_t arg;
  bool     has_arg;
  int      line;
} ConfigBind;

/*
 * One `float` line. A NULL field does not take part in the match, the same
 * way an unset field of the compiled FloatRule does not.
 */
typedef struct ConfigFloatRule {
  char* class_name;
  char* instance_name;
  char* title_substring;
  int   line;
} ConfigFloatRule;

/*
 * A line that was rejected, kept so the caller can report it once with the
 * line number the user will look at.
 */
typedef struct ConfigError {
  int  line;
  char text[CONFIG_FILE_ERROR_TEXT_MAX];
} ConfigError;

typedef struct ConfigFile {
  bool found; /* the path named a readable file */

  ConfigBind      binds[CONFIG_FILE_MAX_BINDS];
  uint32_t        bind_count;
  ConfigFloatRule float_rules[CONFIG_FILE_MAX_FLOAT_RULES];
  uint32_t        float_rule_count;

  /* Tag names by 0-based index; NULL where the file named no tag. */
  char* tag_names[CONFIG_FILE_MAX_TAGS];
  bool  tags_set;
  int   tags_line;

  /* error_count counts every rejected line; errors[] holds the first
   * CONFIG_FILE_MAX_ERRORS of them. */
  ConfigError errors[CONFIG_FILE_MAX_ERRORS];
  uint32_t    error_count;
} ConfigFile;

/*
 * Parse `path`. A file that is not there is not an error: `found` stays false
 * and everything else is empty, which is how the compiled defaults stay in
 * place for a user who has no config file.
 *
 * Returns NULL only when it cannot allocate. The caller owns the result and
 * frees it with config_file_free().
 */
ConfigFile* config_file_load(const char* path);

/* Frees the file and every string it owns. NULL is a no-op. */
void config_file_free(ConfigFile* cf);

/*
 * The path the WM reads when nothing overrides it: $WM_CONFIG, else
 * $XDG_CONFIG_HOME/wm/config, else $HOME/.config/wm/config.
 *
 * Returns false when the path is longer than `size`, or when the environment
 * names no place to look.
 */
bool config_file_default_path(char* buffer, size_t size);

/*
 * The keysym for a key name: a single printable character ("1", "f", "/"), or
 * one of the named keys ("Return", "F5", "Page_Down"). Case does not matter.
 * Returns 0 for a name the keyboard is not told about.
 */
uint32_t config_file_key_keysym(const char* name);

#endif /* CONFIG_FILE_H */
