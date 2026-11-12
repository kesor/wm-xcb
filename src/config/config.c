/*
 * config.c - Configuration system
 *
 * Provides read-only access to configuration values.
 *
 * Two sources, in this order:
 * 1. the user's config file, read by config_load_runtime() before the
 *    components start (config-file.h has the format)
 * 2. the compiled configuration in config.def.h and config.wm.def.h, which
 *    stays in place wherever the file says nothing
 *
 * Note: the keybindings in the file are registered here, through the
 * keybinding binding API. config-file.c only parses them, so the format can be
 * tested without an X server and this module is the only place that knows both
 * the file and the binding table.
 */

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "src/actions/keybinding-binding.h"
#include "src/config/config-file.h"
#include "src/config/config.def.h"
#include "src/config/config.h"
#include "src/config/config.wm.def.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-log.h"
#include "src/core/wm-xcb.h"

/* The file and the tag targets have to agree on how many tags there are, or a
 * `tags` line would name a tag the WM does not have. */
_Static_assert(CONFIG_FILE_MAX_TAGS == CONFIG_TAG_NUM_TAGS, "tag counts differ");

/* Every bind line the parser keeps has to fit the table it is registered into,
 * or a file can name a binding that is parsed and then dropped. The parser
 * bounds its own storage and knows nothing about the binding table, so this is
 * the only place the two numbers meet. */
_Static_assert(CONFIG_FILE_MAX_BINDS <= KEYBINDING_MAX_BINDINGS,
               "the parser accepts more bind lines than the binding table holds");

static ConfigFile* runtime_config = NULL;
static char        runtime_path[PATH_MAX];

/*
 * The keyboard mapping, fetched once and kept.
 *
 * xcb_key_symbols_alloc() waits for the reply to GetKeyboardMapping, and
 * xcb_key_symbols_get_keycode() only reads the table it filled. Holding the
 * table is what lets a SIGHUP resolve the new file's keysyms without a round
 * trip inside the event loop.
 */
static xcb_key_symbols_t* key_symbols = NULL;

/*
 * The compiled rules are `static const` arrays in config.def.h. A file that
 * names rules gets a second table, held here, and the accessors return
 * whichever table is in use. The strings belong to runtime_config and go with
 * it in config_shutdown().
 */
static FloatRule   runtime_float_rules[CONFIG_FILE_MAX_FLOAT_RULES];
static uint32_t    runtime_float_rule_count = 0;
static const char* runtime_tag_names[CONFIG_TAG_NUM_TAGS];

/*
 * Record what the file said about rules and tag names. These need no X
 * connection, so they apply as soon as the file is read; only the bindings
 * wait for config_init(), which needs the server's keymap.
 */
static void
config_apply_runtime(void)
{
  runtime_float_rule_count = 0;
  for (int i = 0; i < CONFIG_TAG_NUM_TAGS; i++)
    runtime_tag_names[i] = NULL;

  if (runtime_config == NULL)
    return;

  for (uint32_t i = 0; i < runtime_config->float_rule_count; i++) {
    const ConfigFloatRule* rule = &runtime_config->float_rules[i];

    runtime_float_rules[i].class_name      = rule->class_name;
    runtime_float_rules[i].instance_name   = rule->instance_name;
    runtime_float_rules[i].title_substring = rule->title_substring;
    runtime_float_rule_count               = i + 1;
  }

  for (int i = 0; i < CONFIG_TAG_NUM_TAGS; i++)
    runtime_tag_names[i] = runtime_config->tag_names[i];
}

bool
config_load_runtime(const char* path)
{
  char default_path[PATH_MAX];

  if (path == NULL) {
    if (!config_file_default_path(default_path, sizeof default_path)) {
      LOG_DEBUG("No HOME and no WM_CONFIG, so there is no config file to read");
      return false;
    }
    path = default_path;
  }

  /* A second load replaces the first. Nothing may reference the old file when
   * this runs: the binding table holds pointers into it. */
  config_shutdown();

  /* Kept even when no file is there: a reload reads this path again, so
   * creating the file and sending SIGHUP has to work, and a file removed
   * before the signal has to bring the compiled configuration back. */
  (void) snprintf(runtime_path, sizeof runtime_path, "%s", path);

  ConfigFile* cf = config_file_load(path);
  if (cf == NULL)
    return false;

  for (uint32_t i = 0; i < cf->error_count && i < CONFIG_FILE_MAX_ERRORS; i++)
    LOG_ERROR("config %s:%d: %s", path, cf->errors[i].line, cf->errors[i].text);
  if (cf->error_count > CONFIG_FILE_MAX_ERRORS)
    LOG_ERROR("config %s: %u more lines were rejected; fix these before reading the rest",
              path, cf->error_count - CONFIG_FILE_MAX_ERRORS);

  if (!cf->found) {
    config_file_free(cf);
    LOG_DEBUG("No config file at %s; the compiled configuration applies", path);
    return false;
  }

  runtime_config = cf;
  config_apply_runtime();

  LOG_DEBUG("Read %s: %u binding(s), %u float rule(s)%s",
            runtime_path, cf->bind_count, cf->float_rule_count,
            cf->tags_set ? ", tag names" : "");
  return true;
}

/*
 * Drop the file and everything that pointed into it. The binding table holds
 * the action names, so it has to be empty before this runs; config_shutdown()
 * relies on the keybinding component having gone first, and config_reload()
 * empties the table itself.
 */
static void
config_release_file(void)
{
  config_file_free(runtime_config);
  runtime_config           = NULL;
  runtime_float_rule_count = 0;
  for (int i = 0; i < CONFIG_TAG_NUM_TAGS; i++)
    runtime_tag_names[i] = NULL;
}

void
config_shutdown(void)
{
  config_release_file();
  runtime_path[0] = '\0';

  if (key_symbols != NULL) {
    xcb_key_symbols_free(key_symbols);
    key_symbols = NULL;
  }
}

/*
 * One error line, whatever the file held: a reload the user asked for is
 * answered with the first thing to fix, and the count of the rest, not with a
 * page of parser output the user did not scroll to.
 */
static void
config_report_reload_error(const ConfigFile* cf)
{
  const ConfigError* first = &cf->errors[0];
  uint32_t           more  = cf->error_count - 1;

  if (more == 0) {
    LOG_ERROR("config %s:%d: %s; the running configuration is unchanged",
              runtime_path, first->line, first->text);
  } else {
    LOG_ERROR("config %s:%d: %s (and %u more rejected line%s); the running configuration is unchanged",
              runtime_path, first->line, first->text, more, more == 1 ? "" : "s");
  }
}

bool
config_reload(void)
{
  if (runtime_path[0] == '\0') {
    LOG_ERROR("cannot reload the configuration: the WM started without a path to look at");
    return false;
  }

  /* The parser runs on the whole file before anything is replaced, so a line it
   * rejects costs the reload rather than half the configuration. */
  ConfigFile* fresh = config_file_load(runtime_path);
  if (fresh == NULL)
    return false;

  if (fresh->error_count > 0) {
    config_report_reload_error(fresh);
    config_file_free(fresh);
    return false;
  }

  /* The table holds pointers into the file that is about to go, so it is empty
   * before the file is freed, not after the new one is wired. */
  keybinding_binding_clear();
  config_release_file();
  runtime_config = fresh;
  config_apply_runtime();
  uint32_t wired = config_init();

  /* The subscribers take the new chords and the new tag names before the line
   * that says the reload applied, so a user who acts on that line does not
   * race the grabs. */
  hub_emit(EVT_CONFIG_RELOADED, TARGET_ID_NONE, NULL);

  if (!fresh->found) {
    LOG_INFO("config %s: no readable file; the compiled configuration applies", runtime_path);
  } else {
    LOG_INFO("config %s: reloaded %u binding(s), %u float rule(s)%s",
             runtime_path, wired, fresh->float_rule_count,
             fresh->tags_set ? ", tag names" : "");
  }

  return true;
}

/*
 * The keyboard mapping, allocated on first use and kept for the process.
 *
 * xcb_key_symbols_alloc() is the call that waits for the reply to
 * GetKeyboardMapping; config_init() runs it before the event loop starts. A
 * SIGHUP reload reads the table again instead of asking the server, which is
 * what keeps the reload out of the loop's way.
 */
static xcb_key_symbols_t*
config_key_symbols(void)
{
  if (key_symbols == NULL && dpy != NULL) {
    /* allow-blocking: config_init() runs before the event loop starts, and a
       reload reuses what this fetched */
    key_symbols = xcb_key_symbols_alloc(dpy);
  }

  return key_symbols;
}

/*
 * The keycode the server gave the keysym, or XCB_NO_SYMBOL when the keyboard
 * has no key for it. Every shift level is searched, so a binding on a
 * character that needs a modifier to type still finds its key.
 */
static xcb_keycode_t
binding_keycode(xcb_key_symbols_t* symbols, const ConfigBind* bind)
{
  xcb_keycode_t  keycode = XCB_NO_SYMBOL;
  xcb_keycode_t* keycodes;

  if (symbols == NULL)
    return XCB_NO_SYMBOL;

  /* allow-blocking: reads the table config_key_symbols() fetched before the
     event loop started; get_keycode sends no request of its own */
  keycodes = xcb_key_symbols_get_keycode(symbols, (xcb_keysym_t) bind->keysym);
  if (keycodes == NULL)
    return XCB_NO_SYMBOL;

  keycode = keycodes[0];
  free(keycodes);
  return keycode;
}

/*
 * Register the bindings the file named. A file with no bind line leaves the
 * compiled bindings in place, so adding one float rule does not take every key
 * away; the same happens when none of the file's bindings can be used, so a
 * file full of typos cannot leave the WM without keys.
 *
 * The table says why it refused a line and this says where the line is: the two
 * causes need different fixes, and a user reading a page of startup output
 * cannot tell them apart from one sentence.
 *
 * Returns how many of the file's lines reached the table.
 */
static uint32_t
config_wire_file_bindings(void)
{
  xcb_key_symbols_t* symbols = config_key_symbols();
  uint32_t           bound   = 0;

  for (uint32_t i = 0; i < runtime_config->bind_count; i++) {
    const ConfigBind*        bind    = &runtime_config->binds[i];
    xcb_keycode_t            keycode = binding_keycode(symbols, bind);
    KeybindingRegisterResult result;

    if (keycode == XCB_NO_SYMBOL) {
      LOG_ERROR("config %s:%d: this keyboard has no key for '%s'",
                runtime_path, bind->line, bind->key);
      continue;
    }

    if (bind->has_arg)
      result = keybinding_binding_register_with_arg(bind->modifiers, keycode, bind->action, bind->arg);
    else
      result = keybinding_binding_register(bind->modifiers, keycode, bind->action);

    switch (result) {
    case KEYBINDING_REGISTERED:
      bound++;
      break;
    case KEYBINDING_ALREADY_BOUND: {
      const KeyBinding* taken = keybinding_binding_lookup(bind->modifiers, keycode);
      LOG_ERROR("config %s:%d: cannot bind '%s' to '%s': the chord is already bound to '%s'",
                runtime_path, bind->line, bind->key, bind->action,
                taken != NULL && taken->action != NULL ? taken->action : "(none)");
      break;
    }
    case KEYBINDING_TABLE_FULL:
      LOG_ERROR("config %s:%d: cannot bind '%s' to '%s': the table holds all %d bindings",
                runtime_path, bind->line, bind->key, bind->action, KEYBINDING_MAX_BINDINGS);
      break;
    case KEYBINDING_REFUSED:
      LOG_ERROR("config %s:%d: cannot bind '%s' to '%s': the binding was refused",
                runtime_path, bind->line, bind->key, bind->action);
      break;
    }
  }

  if (bound == 0) {
    LOG_ERROR("no binding in %s could be used; the compiled bindings are in place",
              runtime_path);
    config_wire_keybindings();
  }

  return bound;
}

/*
 * Initialize the configuration system.
 * This wires keybindings from the config file, or from config.wm.def.h when the
 * file named none, and answers with how many of the file's lines were used.
 */
uint32_t
config_init(void)
{
  LOG_DEBUG("Initializing configuration");

  if (runtime_config == NULL || runtime_config->bind_count == 0) {
    config_wire_keybindings();
    return 0;
  }

  return config_wire_file_bindings();
}

/*
 * Check if a string matches a pattern (substring match).
 * If pattern is NULL or empty, it matches everything.
 */
static bool
string_matches(const char* str, const char* pattern)
{
  if (pattern == NULL || pattern[0] == '\0') {
    return true; /* No pattern = match everything */
  }

  if (str == NULL || str[0] == '\0') {
    return false; /* Have pattern but no string to match */
  }

  return strstr(str, pattern) != NULL;
}

/*
 * Match a rule against window properties
 */
static bool
rule_matches(
    const char* class_name,
    const char* instance_name,
    const char* title,
    const char* rule_class,
    const char* rule_instance,
    const char* rule_title)
{
  /* Check class_name match */
  if (!string_matches(class_name, rule_class)) {
    return false;
  }

  /* Check instance_name match */
  if (!string_matches(instance_name, rule_instance)) {
    return false;
  }

  /* Check title match */
  if (!string_matches(title, rule_title)) {
    return false;
  }

  return true;
}

/*
 * ============================================================================
 * Configuration Access Functions
 * ============================================================================
 */

const FloatRule*
config_get_float_rules(void)
{
  return runtime_float_rule_count > 0 ? runtime_float_rules : default_float_rules;
}

uint32_t
config_get_float_rule_count(void)
{
  return runtime_float_rule_count > 0 ? runtime_float_rule_count : DEFAULT_FLOAT_RULE_COUNT;
}

const TagRule*
config_get_tag_rules(void)
{
  return default_tag_rules;
}

uint32_t
config_get_tag_rule_count(void)
{
  return DEFAULT_TAG_RULE_COUNT;
}

const char*
config_get_tag_name(int index)
{
  if (index < 0 || index >= CONFIG_TAG_NUM_TAGS)
    return NULL;
  return runtime_tag_names[index];
}

const GapConfig*
config_get_gaps(void)
{
  return &default_gaps;
}

const BorderConfig*
config_get_borders(void)
{
  return &default_borders;
}

uint32_t
config_get_tag_count(void)
{
  return CONFIG_TAG_NUM_TAGS;
}

/*
 * ============================================================================
 * Rule Matching Functions
 * ============================================================================
 */

bool
config_should_float(const char* class_name, const char* instance_name, const char* title)
{
  const FloatRule* rules = config_get_float_rules();
  uint32_t         count = config_get_float_rule_count();

  /* Check all float rules */
  for (uint32_t i = 0; i < count; i++) {
    const FloatRule* rule = &rules[i];

    /* Empty rule (all NULL) - skip (allows matching all windows) */
    if (rule->class_name == NULL && rule->instance_name == NULL && rule->title_substring == NULL) {
      LOG_DEBUG("Float rule %u: empty rule (skipping)", i);
      continue;
    }

    if (rule_matches(class_name, instance_name, title,
                     rule->class_name, rule->instance_name, rule->title_substring)) {
      LOG_DEBUG("Float rule %u matches: class=%s instance=%s title=%s",
                i, class_name ? class_name : "(null)",
                instance_name ? instance_name : "(null)",
                title ? title : "(null)");
      return true;
    }
  }

  return false;
}

uint32_t
config_get_tags(const char* class_name, const char* instance_name, const char* title)
{
  /* Check all tag rules - first match wins */
  for (uint32_t i = 0; i < DEFAULT_TAG_RULE_COUNT; i++) {
    const TagRule* rule = &default_tag_rules[i];

    if (rule_matches(class_name, instance_name, title,
                     rule->class_name, rule->instance_name, rule->title_substring)) {
      LOG_DEBUG("Tag rule %u matches: class=%s instance=%s title=%s -> tags=%" PRIu32,
                i, class_name ? class_name : "(null)",
                instance_name ? instance_name : "(null)",
                title ? title : "(null)", rule->tags);
      return rule->tags;
    }
  }

  /* No matching rule - use default */
  return CONFIG_TAG_DEFAULT_MASK;
}
