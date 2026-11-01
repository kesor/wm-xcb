/*
 * test-terminal.c - Tests for terminal spawning module
 *
 * Tests:
 * - terminal_init and terminal_shutdown
 * - action registration and lookup
 * - action callback execution
 * - default terminal command
 * - custom terminal command
 */

#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/actions/action-registry.h"
#include "src/actions/terminal.h"
#include "wm-log.h"

#include "test-registry.h"
#include "test-terminal.h"

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
 * Test: default terminal command
 */
void
test_terminal_default_command(void)
{
  LOG_CLEAN("  test_terminal_default_command...");

  /* Clear any custom command first */
  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  /* Clear TERMINAL env var */
  unsetenv("TERMINAL");

  /* Should return xterm */
  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "xterm") == 0);

  terminal_shutdown();
  action_registry_shutdown();
}

/*
 * Test: custom terminal command
 */
void
test_terminal_custom_command(void)
{
  LOG_CLEAN("  test_terminal_custom_command...");

  action_registry_init();
  terminal_init();

  /* Save original TERMINAL env var if set */
  const char* orig_term  = getenv("TERMINAL");
  char*       saved_term = NULL;
  if (orig_term != NULL) {
    saved_term = strdup(orig_term);
    unsetenv("TERMINAL");
  }

  /* Set a custom command */
  terminal_set_command("my-terminal --flag");

  /* Should return our custom command */
  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "my-terminal --flag") == 0);

  /* Clear custom command */
  terminal_set_command(NULL);

  /* Should fall back to default (xterm without env var) */
  cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "xterm") == 0);

  /* Restore original TERMINAL env var */
  if (saved_term != NULL) {
    setenv("TERMINAL", saved_term, 1);
    free(saved_term);
  }

  terminal_shutdown();
  action_registry_shutdown();
}

/*
 * Test: terminal command with environment variable
 */
void
test_terminal_env_variable(void)
{
  LOG_CLEAN("  test_terminal_env_variable...");

  action_registry_init();
  terminal_init();

  /* Clear any custom command first */
  terminal_set_command(NULL);

  /* Save original TERMINAL env var */
  const char* orig_term  = getenv("TERMINAL");
  char*       saved_term = NULL;
  if (orig_term != NULL) {
    saved_term = strdup(orig_term);
  }

  /* Set TERMINAL env var to a test value */
  setenv("TERMINAL", "alacritty -e tmux", 1);

  /* Should return the env var value */
  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "alacritty -e tmux") == 0);

  /* Clear env var */
  unsetenv("TERMINAL");

  /* Restore original TERMINAL env var */
  if (saved_term != NULL) {
    setenv("TERMINAL", saved_term, 1);
    free(saved_term);
  }

  terminal_shutdown();
  action_registry_shutdown();
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
  test_terminal_default_command();
  test_terminal_custom_command();
  test_terminal_env_variable();
  test_terminal_action_target_type();

  LOG_CLEAN("  All terminal tests passed!");
}

TEST_GROUP(TerminalModule, {
  test_terminal_action_exists();
  test_terminal_double_init();
  test_terminal_shutdown();
  test_terminal_default_command();
  test_terminal_custom_command();
  test_terminal_env_variable();
  test_terminal_action_target_type();
});