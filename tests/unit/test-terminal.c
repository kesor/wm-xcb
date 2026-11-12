/*
 * test-terminal.c - Tests for terminal spawning module
 *
 * Tests:
 * - terminal_init and terminal_shutdown
 * - action registration and lookup
 * - action callback execution
 * - which installed terminal the resolver picks, and what it says when there
 *   is none
 * - custom terminal command
 *
 * The resolver asks the filesystem and $PATH, so a test that leaves them to the
 * machine running the suite asserts whatever that machine has installed. Each
 * test here puts PATH in a directory of its own and installs the programs it
 * expects the resolver to find.
 */

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/actions/action-registry.h"
#include "src/actions/terminal.h"
#include "src/core/wm-log.h"

#include "tests/unit/test-registry.h"
#include "tests/unit/test-terminal.h"

/*
 * A PATH of this test's own, holding only the programs the test installs.
 */
typedef struct {
  char  dir[PATH_MAX];
  char* saved_path;
  char* saved_terminal;
} TerminalPath;

static void
terminal_path_begin(TerminalPath* p)
{
  const char* path = getenv("PATH");
  const char* term = getenv("TERMINAL");

  p->saved_path     = path != NULL ? strdup(path) : NULL;
  p->saved_terminal = term != NULL ? strdup(term) : NULL;

  snprintf(p->dir, sizeof(p->dir), "/tmp/wm-terminal-test-XXXXXX");
  assert(mkdtemp(p->dir) != NULL);
  assert(setenv("PATH", p->dir, 1) == 0);
  unsetenv("TERMINAL");
}

/* Installs an executable named <program> in the test's directory. */
static void
terminal_path_install(TerminalPath* p, const char* program)
{
  char* path = NULL;
  FILE* file;

  assert(asprintf(&path, "%s/%s", p->dir, program) > 0);
  file = fopen(path, "w");
  assert(file != NULL);
  assert(fputs("#!/bin/sh\nexit 0\n", file) >= 0);
  assert(fclose(file) == 0);
  assert(chmod(path, 0755) == 0);
  free(path);
}

static void
terminal_path_end(TerminalPath* p)
{
  DIR*           dir = opendir(p->dir);
  struct dirent* entry;

  if (dir != NULL) {
    while ((entry = readdir(dir)) != NULL) {
      char* path = NULL;

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      assert(asprintf(&path, "%s/%s", p->dir, entry->d_name) > 0);
      unlink(path);
      free(path);
    }
    closedir(dir);
  }
  rmdir(p->dir);

  if (p->saved_path != NULL) {
    setenv("PATH", p->saved_path, 1);
    free(p->saved_path);
  } else {
    unsetenv("PATH");
  }

  if (p->saved_terminal != NULL) {
    setenv("TERMINAL", p->saved_terminal, 1);
    free(p->saved_terminal);
  } else {
    unsetenv("TERMINAL");
  }
}

/*
 * Test: terminal action exists after init
 */
void
test_terminal_action_exists(void)
{
  LOG_CLEAN("  test_terminal_action_exists...");

  action_registry_init();
  terminal_init();

  /* Check that terminal.spawn action is registered */
  Action* action = action_lookup("terminal.spawn");
  assert(action != NULL);

  /* Verify action properties */
  assert(strcmp(action->name, "terminal.spawn") == 0);
  assert(action->callback == terminal_action_spawn);
  assert(action->target_type == ACTION_TARGET_NONE);

  terminal_shutdown();
  action_registry_shutdown();
}

/*
 * Test: terminal double init is safe
 */
void
test_terminal_double_init(void)
{
  LOG_CLEAN("  test_terminal_double_init...");

  action_registry_init();
  terminal_init();
  terminal_init(); /* Should be safe */

  terminal_shutdown();
  action_registry_shutdown();
}

/*
 * Test: terminal shutdown unregisters action
 */
void
test_terminal_shutdown(void)
{
  LOG_CLEAN("  test_terminal_shutdown...");

  action_registry_init();
  terminal_init();

  /* Action should exist */
  assert(action_lookup("terminal.spawn") != NULL);

  terminal_shutdown();

  /* Action should be gone */
  assert(action_lookup("terminal.spawn") == NULL);

  action_registry_shutdown();
}

/*
 * Test: a terminal the machine does not have is not used
 */
void
test_terminal_picks_installed_candidate(void)
{
  LOG_CLEAN("  test_terminal_picks_installed_candidate...");

  TerminalPath path;
  terminal_path_begin(&path);
  terminal_path_install(&path, "xterm");

  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "xterm") == 0);

  terminal_shutdown();
  action_registry_shutdown();
  terminal_path_end(&path);
}

/*
 * Test: the distribution's own choice beats a terminal that is merely present
 */
void
test_terminal_prefers_x_terminal_emulator(void)
{
  LOG_CLEAN("  test_terminal_prefers_x_terminal_emulator...");

  TerminalPath path;
  terminal_path_begin(&path);
  terminal_path_install(&path, "xterm");
  terminal_path_install(&path, "x-terminal-emulator");

  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "x-terminal-emulator") == 0);

  terminal_shutdown();
  action_registry_shutdown();
  terminal_path_end(&path);
}

/*
 * Test: custom terminal command
 */
void
test_terminal_custom_command(void)
{
  LOG_CLEAN("  test_terminal_custom_command...");

  TerminalPath path;
  terminal_path_begin(&path);
  terminal_path_install(&path, "xterm");

  action_registry_init();
  terminal_init();

  /* A command set here is what the caller asked for: it is not looked up on
   * PATH, so it wins even when nothing installs it. */
  terminal_set_command("my-terminal --flag");

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "my-terminal --flag") == 0);

  /* Clearing it hands the choice back to the resolver. */
  terminal_set_command(NULL);

  cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "xterm") == 0);

  terminal_shutdown();
  action_registry_shutdown();
  terminal_path_end(&path);
}

/*
 * Test: $TERMINAL wins when the program it names is installed, arguments and
 * all
 */
void
test_terminal_env_variable(void)
{
  LOG_CLEAN("  test_terminal_env_variable...");

  TerminalPath path;
  terminal_path_begin(&path);
  terminal_path_install(&path, "myterm");
  terminal_path_install(&path, "xterm");

  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  assert(setenv("TERMINAL", "myterm -e tmux", 1) == 0);

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "myterm -e tmux") == 0);

  terminal_shutdown();
  action_registry_shutdown();
  terminal_path_end(&path);
}

/*
 * Test: a $TERMINAL naming a program this machine lacks falls through to one
 * that is installed, instead of leaving MOD+Return doing nothing
 */
void
test_terminal_skips_uninstalled_env(void)
{
  LOG_CLEAN("  test_terminal_skips_uninstalled_env...");

  TerminalPath path;
  terminal_path_begin(&path);
  terminal_path_install(&path, "xterm");

  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  assert(setenv("TERMINAL", "not-installed-anywhere -e tmux", 1) == 0);

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "xterm") == 0);

  terminal_shutdown();
  action_registry_shutdown();
  terminal_path_end(&path);
}

/*
 * Test: with no terminal installed the resolver returns nothing and the report
 * names what was tried, so the user can install one of them
 */
void
test_terminal_reports_what_it_tried(void)
{
  LOG_CLEAN("  test_terminal_reports_what_it_tried...");

  TerminalPath path;
  terminal_path_begin(&path);

  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  assert(terminal_get_default() == NULL);

  const char* report = terminal_missing_report();
  assert(report != NULL);
  assert(strstr(report, "$TERMINAL") != NULL);
  assert(strstr(report, "x-terminal-emulator") != NULL);
  assert(strstr(report, "alacritty") != NULL);
  assert(strstr(report, "kitty") != NULL);
  assert(strstr(report, "xterm") != NULL);

  /* Nothing was installed, so the press must report rather than fork a shell
   * that prints its own "not found" line. */
  assert(terminal_spawn() == false);

  terminal_shutdown();
  action_registry_shutdown();
  terminal_path_end(&path);
}

/*
 * Test: action registration uses proper target type
 */
void
test_terminal_action_target_type(void)
{
  LOG_CLEAN("  test_terminal_action_target_type...");

  action_registry_init();
  terminal_init();

  Action* action = action_lookup("terminal.spawn");
  assert(action != NULL);

  /* Verify it's a no-target action */
  assert(action->target_type == ACTION_TARGET_NONE);
  assert(action->target_required == false);
  assert(action->target_resolver == NULL);

  terminal_shutdown();
  action_registry_shutdown();
}

/*
 * Run all terminal tests
 */
void
test_terminal_run_all(void)
{
  LOG_CLEAN("=== Terminal Tests ===");

  test_terminal_action_exists();
  test_terminal_double_init();
  test_terminal_shutdown();
  test_terminal_picks_installed_candidate();
  test_terminal_prefers_x_terminal_emulator();
  test_terminal_custom_command();
  test_terminal_env_variable();
  test_terminal_skips_uninstalled_env();
  test_terminal_reports_what_it_tried();
  test_terminal_action_target_type();

  LOG_CLEAN("  All terminal tests passed!");
}

TEST_GROUP(TerminalModule, {
  test_terminal_action_exists();
  test_terminal_double_init();
  test_terminal_shutdown();
  test_terminal_picks_installed_candidate();
  test_terminal_prefers_x_terminal_emulator();
  test_terminal_custom_command();
  test_terminal_env_variable();
  test_terminal_skips_uninstalled_env();
  test_terminal_reports_what_it_tried();
  test_terminal_action_target_type();
});