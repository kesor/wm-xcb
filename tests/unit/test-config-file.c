/*
 * Config file tests
 *
 * The format is the user's interface: a line it cannot read has to cost that
 * line and nothing else, and the error it reports has to name the line the
 * user will open the file at. Parsing touches no X server, so all of that is
 * testable here. Wiring the parsed bindings onto real keycodes, and what the
 * X server does with them, is tests/x/test-54-config-file-binding.sh.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/keysym.h>

#include "config-file.h"
#include "config.def.h"
#include "config.h"
#include "src/actions/keybinding-binding.h"
#include "src/components/keybinding.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"

/*
 * Write `text` to a fresh file and put its path in `path`. The caller unlinks
 * it. Returns false when the temporary file cannot be made.
 */
static bool
write_config(char* path, size_t size, const char* text)
{
  const char* dir = getenv("TMPDIR");
  int         fd  = -1;
  FILE*       file;

  if (dir == NULL || dir[0] == '\0')
    dir = "/tmp";
  if (snprintf(path, size, "%s/wm-config-test-XXXXXX", dir) >= (int) size)
    return false;

  fd = mkstemp(path);
  if (fd < 0)
    return false;

  file = fdopen(fd, "w");
  if (file == NULL) {
    close(fd);
    unlink(path);
    return false;
  }

  (void) fputs(text, file);
  (void) fclose(file);
  return true;
}

void
test_config_file_missing(void)
{
  LOG_CLEAN("== Testing a config file that is not there");

  ConfigFile* cf = config_file_load("/nonexistent/wm/config");

  assert(cf != NULL);
  assert(cf->found == false);
  assert(cf->bind_count == 0);
  assert(cf->float_rule_count == 0);
  assert(cf->error_count == 0);

  config_file_free(cf);
}

void
test_config_file_parses_bindings(void)
{
  LOG_CLEAN("== Testing bind lines");

  char       path[256];
  const char text[] =
      "# bindings the compiled configuration does not have\n"
      "bind Mod+F5        tag-manager.view 2\n"
      "bind Mod+Shift+F6  tag-manager.client-move 3   # a trailing comment\n"
      "bind Mod+Return    terminal.spawn\n"
      "bind Ctrl+alt+x    app.launch\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  ConfigFile* cf = config_file_load(path);

  assert(cf != NULL && cf->found);
  assert(cf->error_count == 0);
  assert(cf->bind_count == 4);

  assert(cf->binds[0].modifiers == XCB_MOD_MASK_4);
  assert(strcmp(cf->binds[0].key, "F5") == 0);
  assert(cf->binds[0].keysym == XK_F5);
  assert(strcmp(cf->binds[0].action, "tag-manager.view") == 0);
  assert(cf->binds[0].has_arg && cf->binds[0].arg == 2);
  assert(cf->binds[0].line == 2);

  assert(cf->binds[1].modifiers == (XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT));
  assert(cf->binds[1].arg == 3);
  assert(cf->binds[1].line == 3);

  assert(cf->binds[2].has_arg == false);
  assert(strcmp(cf->binds[2].action, "terminal.spawn") == 0);

  /* The chord is case-insensitive, and Shift has to be written as a modifier
   * even for a capital key name. */
  assert(cf->binds[3].modifiers == (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1));
  assert(cf->binds[3].keysym == (uint32_t) 'x');

  config_file_free(cf);
  unlink(path);
}

void
test_config_file_reports_the_line(void)
{
  LOG_CLEAN("== Testing malformed lines carry their line number");

  char       path[256];
  const char text[] =
      "bind Mod+1 tag-manager.view 1\n"
      "bogus directive\n"
      "bind Hyper+1 tag-manager.view 1\n"
      "bind Mod+Nope tag-manager.view 1\n"
      "bind Mod+2\n"
      "bind Mod+3 tag-manager.view two\n"
      "bind Return terminal.spawn\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  ConfigFile* cf = config_file_load(path);

  assert(cf != NULL);
  /* The one good line survives: a typo costs that line, not the file. */
  assert(cf->bind_count == 1);
  assert(cf->error_count == 6);

  assert(cf->errors[0].line == 2);
  assert(strstr(cf->errors[0].text, "bogus") != NULL);
  assert(cf->errors[1].line == 3);
  assert(strstr(cf->errors[1].text, "Hyper") != NULL);
  assert(cf->errors[2].line == 4);
  assert(strstr(cf->errors[2].text, "Nope") != NULL);
  assert(cf->errors[3].line == 5);
  assert(cf->errors[4].line == 6);
  assert(strstr(cf->errors[4].text, "two") != NULL);
  /* A grab on a plain key would take that key from every client, so the chord
   * has to name a modifier. */
  assert(cf->errors[5].line == 7);
  assert(strstr(cf->errors[5].text, "modifier") != NULL);

  config_file_free(cf);
  unlink(path);
}

void
test_config_file_parses_float_rules(void)
{
  LOG_CLEAN("== Testing float lines");

  char       path[256];
  const char text[] =
      "float class=Pavucontrol\n"
      "float title=\" - Preferences\"\n"
      "float instance=gimp class=Gimp title=Toolbox\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  ConfigFile* cf = config_file_load(path);

  assert(cf != NULL);
  assert(cf->error_count == 0);
  assert(cf->float_rule_count == 3);

  assert(cf->float_rules[0].class_name != NULL && strcmp(cf->float_rules[0].class_name, "Pavucontrol") == 0);
  assert(cf->float_rules[0].instance_name == NULL);
  assert(cf->float_rules[0].title_substring == NULL);
  assert(cf->float_rules[0].line == 1);

  assert(cf->float_rules[1].title_substring != NULL && strcmp(cf->float_rules[1].title_substring, " - Preferences") == 0);

  assert(cf->float_rules[2].class_name != NULL && strcmp(cf->float_rules[2].class_name, "Gimp") == 0);
  assert(cf->float_rules[2].instance_name != NULL && strcmp(cf->float_rules[2].instance_name, "gimp") == 0);
  assert(cf->float_rules[2].title_substring != NULL && strcmp(cf->float_rules[2].title_substring, "Toolbox") == 0);

  config_file_free(cf);
  unlink(path);
}

void
test_config_file_rejects_bad_float_and_tags_lines(void)
{
  LOG_CLEAN("== Testing float and tags lines that cannot be used");

  char       path[256];
  const char text[] =
      "float class=\n"
      "float nope=x\n"
      "float\n"
      "tags\n"
      "tags a b c d e f g h i j\n"
      "tags \"\"\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  ConfigFile* cf = config_file_load(path);

  assert(cf != NULL);
  assert(cf->float_rule_count == 0);
  assert(cf->tags_set == false);
  for (int i = 0; i < CONFIG_FILE_MAX_TAGS; i++)
    assert(cf->tag_names[i] == NULL);
  assert(cf->error_count == 6);

  config_file_free(cf);
  unlink(path);
}

void
test_config_file_parses_tag_names(void)
{
  LOG_CLEAN("== Testing tags lines");

  char       path[256];
  const char text[] =
      "tags 1 2 3 4 dev \"my chat\"\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  ConfigFile* cf = config_file_load(path);

  assert(cf != NULL);
  assert(cf->error_count == 0);
  assert(cf->tags_set == true);
  assert(cf->tag_names[4] != NULL && strcmp(cf->tag_names[4], "dev") == 0);
  assert(cf->tag_names[5] != NULL && strcmp(cf->tag_names[5], "my chat") == 0);
  assert(cf->tag_names[6] == NULL);

  config_file_free(cf);
  unlink(path);
}

void
test_config_file_key_names(void)
{
  LOG_CLEAN("== Testing key names");

  assert(config_file_key_keysym("1") == (uint32_t) '1');
  assert(config_file_key_keysym("f") == (uint32_t) 'f');
  assert(config_file_key_keysym("F") == (uint32_t) 'f');
  assert(config_file_key_keysym("F5") == XK_F5);
  assert(config_file_key_keysym("f5") == XK_F5);
  assert(config_file_key_keysym("Return") == XK_Return);
  assert(config_file_key_keysym("page_down") == XK_Page_Down);
  assert(config_file_key_keysym("nope") == 0);
  assert(config_file_key_keysym("") == 0);
  assert(config_file_key_keysym(NULL) == 0);
}

void
test_config_file_default_path(void)
{
  LOG_CLEAN("== Testing where the WM looks for the file");

  char  buffer[512];
  char* saved_config = strdup(getenv("WM_CONFIG") ? getenv("WM_CONFIG") : "");
  char* saved_xdg    = strdup(getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "");
  char* saved_home   = strdup(getenv("HOME") ? getenv("HOME") : "");

  (void) setenv("WM_CONFIG", "/tmp/because-the-user-said-so", 1);
  (void) setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
  assert(config_file_default_path(buffer, sizeof buffer));
  assert(strcmp(buffer, "/tmp/because-the-user-said-so") == 0);

  (void) unsetenv("WM_CONFIG");
  assert(config_file_default_path(buffer, sizeof buffer));
  assert(strcmp(buffer, "/tmp/xdg/wm/config") == 0);

  (void) unsetenv("XDG_CONFIG_HOME");
  (void) setenv("HOME", "/tmp/elsewhere", 1);
  assert(config_file_default_path(buffer, sizeof buffer));
  assert(strcmp(buffer, "/tmp/elsewhere/.config/wm/config") == 0);

  (void) unsetenv("HOME");
  assert(config_file_default_path(buffer, sizeof buffer) == false);

  /* Leave the environment as it was: the rest of the suite runs in this
   * process. */
  if (saved_config[0] != '\0')
    (void) setenv("WM_CONFIG", saved_config, 1);
  else
    (void) unsetenv("WM_CONFIG");
  if (saved_xdg[0] != '\0')
    (void) setenv("XDG_CONFIG_HOME", saved_xdg, 1);
  else
    (void) unsetenv("XDG_CONFIG_HOME");
  if (saved_home[0] != '\0')
    (void) setenv("HOME", saved_home, 1);
  else
    (void) unsetenv("HOME");

  free(saved_config);
  free(saved_xdg);
  free(saved_home);
}

/*
 * A file that names no binding leaves the compiled bindings in place. This is
 * the acceptance case "removing the file falls back to the built-in bindings",
 * seen from the wiring: the table is the compiled one, keycode 36 and all.
 */
void
test_config_file_absent_keeps_compiled_bindings(void)
{
  LOG_CLEAN("== Testing the compiled bindings apply with no config file");

  assert(config_load_runtime("/nonexistent/wm/config") == false);

  hub_init();
  xcb_handler_init();
  keybinding_init();

  assert(keybinding_get_count() > 0);
  const KeyBinding* binding = keybinding_lookup(XCB_MOD_MASK_4, 36);
  assert(binding != NULL && strcmp(binding->action, "terminal.spawn") == 0);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
  config_shutdown();
}

/*
 * A file that names rules replaces the compiled ones, and freeing the file
 * puts the compiled rules back: a reader of config_get_float_rules() never
 * holds a pointer into a freed file.
 */
void
test_config_file_float_rules_apply(void)
{
  LOG_CLEAN("== Testing float rules from the file replace the compiled ones");

  char       path[256];
  const char text[] = "float class=Pavucontrol\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  uint32_t compiled = config_get_float_rule_count();
  assert(compiled > 0);

  assert(config_load_runtime(path) == true);
  assert(config_get_float_rule_count() == 1);
  assert(config_should_float("Pavucontrol", NULL, NULL) == true);
  /* "steam" is in the compiled rules, which the file's one rule replaced. */
  assert(config_should_float("steam", NULL, NULL) == false);

  config_shutdown();
  assert(config_get_float_rule_count() == compiled);
  assert(config_should_float("steam", NULL, NULL) == true);

  unlink(path);
}

void
test_config_file_tag_names_reach_tags(void)
{
  LOG_CLEAN("== Testing tag names from the file reach the tag list");

  char       path[256];
  const char text[] = "tags 1 2 3 4 dev chat\n";

  if (!write_config(path, sizeof path, text)) {
    assert(0 && "cannot write a temporary config file");
    return;
  }

  assert(config_load_runtime(path) == true);

  hub_init();
  tag_list_init();

  Tag* named = tag_get_by_index(4);
  assert(named != NULL);
  assert(strcmp(tag_get_name(named), "dev") == 0);

  /* A tag the file did not name keeps the number the user types. */
  Tag* unnamed = tag_get_by_index(6);
  assert(unnamed != NULL);
  assert(strcmp(tag_get_name(unnamed), "7") == 0);

  tag_list_shutdown();
  hub_shutdown();
  config_shutdown();
  unlink(path);
}

TEST_GROUP(ConfigFile, {
  test_config_file_missing();
  test_config_file_parses_bindings();
  test_config_file_reports_the_line();
  test_config_file_parses_float_rules();
  test_config_file_rejects_bad_float_and_tags_lines();
  test_config_file_parses_tag_names();
  test_config_file_key_names();
  test_config_file_default_path();
  test_config_file_absent_keeps_compiled_bindings();
  test_config_file_float_rules_apply();
  test_config_file_tag_names_reach_tags();
});
