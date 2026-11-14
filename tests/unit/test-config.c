/*
 * Config tests
 *
 * What a window rule answers for the strings a window carries.
 *
 * test-config-file.c tests the format: which lines parse and what they turn
 * into. This file tests the matcher, which is what a user sees: a window whose
 * WM_CLASS or title a rule names floats and a window no rule names stays in the
 * layout, and the same for the tags a rule gives it.
 *
 * The strings are the ones the WM read from the server; the manage path that
 * passes them in and acts on the answer is
 * tests/unit/test-client-list-component.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/config/config.def.h"
#include "src/config/config.h"
#include "src/core/wm-log.h"
#include "tests/unit/test-registry.h"
#include "tests/unit/test-wm.h"

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
  if (snprintf(path, size, "%s/wm-config-rules-XXXXXX", dir) >= (int) size)
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

/*
 * The compiled class rule names "steam", and a window it does not name keeps the
 * layout.
 */
void
test_config_float_rule_matches_a_class(void)
{
  LOG_CLEAN("== Testing a float rule matches a window class");

  config_shutdown();

  assert(config_should_float("steam", "steam", "Steam") == true);
  assert(config_should_float("xterm", "xterm", "xterm") == false);
}

/*
 * The compiled title rules carry a leading space and a dash: they match a title
 * that appends the word to something else, not a window called "Settings".
 */
void
test_config_float_rule_matches_a_title_substring(void)
{
  LOG_CLEAN("== Testing a float rule matches part of a window title");

  config_shutdown();

  assert(config_should_float("gimp", "gimp", "GNU Image Manipulation Program - Preferences") == true);
  assert(config_should_float("gimp", "gimp", "GNU Image Manipulation Program - Settings") == true);
  assert(config_should_float("gimp", "gimp", "GNU Image Manipulation Program") == false);
  assert(config_should_float("firefox", "Navigator", "Settings") == false);
}

/*
 * A rule's pattern has nothing to match against a window that never set the
 * property, or set it to the empty string. A rule with no pattern for that field
 * does not care what the window carries.
 */
void
test_config_float_rule_does_not_match_a_missing_field(void)
{
  LOG_CLEAN("== Testing a rule against a window with no class or title");

  config_shutdown();

  assert(config_should_float(NULL, NULL, NULL) == false);
  assert(config_should_float("", "", "") == false);
  /* The title rules name no class, so a window with no class can still match on
   * its title. */
  assert(config_should_float(NULL, NULL, "GNU Image Manipulation Program - Settings") == true);
}

/*
 * Every field a rule names has to match: the fields narrow each other, they are
 * not alternatives.
 */
void
test_config_float_rule_fields_all_have_to_match(void)
{
  LOG_CLEAN("== Testing every field of a float rule has to match");

  char       path[256];
  const char text[] = "float class=Pavucontrol instance=pavucontrol title=\"Volume Control\"\n";

  assert_or_abort(write_config(path, sizeof path, text));
  assert_or_abort(config_load_runtime(path) == true);

  assert(config_should_float("Pavucontrol", "pavucontrol", "Volume Control") == true);
  assert(config_should_float("Pavucontrol", "other", "Volume Control") == false);
  assert(config_should_float("Pavucontrol", "pavucontrol", "Something Else") == false);
  assert(config_should_float("Pavucontrol", NULL, "Volume Control") == false);

  config_shutdown();
  unlink(path);
}

/*
 * The compiled tag rules: the browsers belong on one tag, the chat client on
 * another, and a window no rule names keeps the tag a new client gets.
 */
void
test_config_tag_rule_answers_the_rule_tags(void)
{
  LOG_CLEAN("== Testing a tag rule answers the tags it names");

  config_shutdown();

  assert(config_get_tags("firefox", "Navigator", "Mozilla Firefox") == (1u << 1));
  assert(config_get_tags("chromium", "chromium", "Chromium") == (1u << 1));
  assert(config_get_tags("slack", "slack", "Slack") == (1u << 2));
  assert(config_get_tags("xterm", "xterm", "xterm") == CONFIG_TAG_DEFAULT_MASK);
  assert(config_get_tags(NULL, NULL, NULL) == CONFIG_TAG_DEFAULT_MASK);
}

/*
 * With no file read, every accessor answers the compiled configuration, and a
 * tag index the WM does not have has no name.
 */
void
test_config_accessors_answer_the_compiled_configuration(void)
{
  LOG_CLEAN("== Testing the accessors answer the compiled configuration");

  config_shutdown();

  assert(config_get_float_rules() != NULL);
  assert(config_get_float_rule_count() == DEFAULT_FLOAT_RULE_COUNT);
  assert(config_get_tag_rules() != NULL);
  assert(config_get_tag_rule_count() == DEFAULT_TAG_RULE_COUNT);
  assert(config_get_gaps()->inner == default_gaps.inner);
  assert(config_get_borders()->width == default_borders.width);
  assert(config_get_tag_count() == CONFIG_TAG_NUM_TAGS);

  assert(config_get_tag_name(-1) == NULL);
  assert(config_get_tag_name(CONFIG_TAG_NUM_TAGS) == NULL);
  assert(config_get_tag_name(0) == NULL);
}

/*
 * A file with a line the parser rejects still applies the lines it can read: a
 * typo costs that line, not the rest of the configuration.
 */
void
test_config_load_runtime_keeps_the_good_lines(void)
{
  LOG_CLEAN("== Testing a rejected line does not cost the rest of the file");

  char       path[256];
  const char text[] = "float class=Pavucontrol\nbogus directive\ntags dev chat\n";

  assert_or_abort(write_config(path, sizeof path, text));
  assert(config_load_runtime(path) == true);

  assert(config_should_float("Pavucontrol", NULL, NULL) == true);
  assert(config_get_tag_name(0) != NULL && strcmp(config_get_tag_name(0), "dev") == 0);
  assert(config_get_tag_name(1) != NULL && strcmp(config_get_tag_name(1), "chat") == 0);

  config_shutdown();
  unlink(path);
}

/*
 * A reload with no file path has nothing to reread. It refuses rather than
 * clearing the configuration the WM is running with.
 */
void
test_config_reload_without_a_path(void)
{
  LOG_CLEAN("== Testing a reload with no file path is refused");

  config_shutdown();

  assert(config_reload() == false);
  assert(config_should_float("steam", NULL, NULL) == true);
}

/*
 * A path of NULL asks the environment where the file is. A file that is not
 * there is not an error: the compiled rules are what apply.
 */
void
test_config_load_runtime_without_a_path(void)
{
  LOG_CLEAN("== Testing a load with no path follows the environment");

  char* saved_config = strdup(getenv("WM_CONFIG") ? getenv("WM_CONFIG") : "");
  char* saved_xdg    = strdup(getenv("XDG_CONFIG_HOME") ? getenv("XDG_CONFIG_HOME") : "");
  char* saved_home   = strdup(getenv("HOME") ? getenv("HOME") : "");

  config_shutdown();

  /* No variable says where a file is: there is nothing to read. */
  (void) unsetenv("WM_CONFIG");
  (void) unsetenv("XDG_CONFIG_HOME");
  (void) unsetenv("HOME");
  assert(config_load_runtime(NULL) == false);

  /* The variable names a file that is not there. */
  (void) setenv("WM_CONFIG", "/nonexistent/wm/config", 1);
  assert(config_load_runtime(NULL) == false);

  assert(config_should_float("steam", NULL, NULL) == true);

  /* Leave the environment as it was: the rest of the suite runs in this
   * process. A load that found no file still records the path, and a later
   * reload in this process would reread it instead of refusing. */
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

  config_shutdown();
  assert(config_reload() == false);

  free(saved_config);
  free(saved_xdg);
  free(saved_home);
}

TEST_GROUP(Config, {
  test_config_float_rule_matches_a_class();
  test_config_float_rule_matches_a_title_substring();
  test_config_float_rule_does_not_match_a_missing_field();
  test_config_float_rule_fields_all_have_to_match();
  test_config_tag_rule_answers_the_rule_tags();
  test_config_accessors_answer_the_compiled_configuration();
  test_config_load_runtime_keeps_the_good_lines();
  test_config_reload_without_a_path();
  test_config_load_runtime_without_a_path();
});
