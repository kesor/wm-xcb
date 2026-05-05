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
 * Block SIGCHLD during test to prevent signal delivery during assertions.
 * This is needed because the terminal spawn action may have SIGCHLD handling
 * that could interfere with test assertions.
 */
static void
block_sigchld(void)
{
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, NULL);
}

static void
unblock_sigchld(void)
{
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

static void
reap_children(void)
{
  /* Reap any zombie children */
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

/*
 * Test: terminal action exists after init
 */
void
test_terminal_action_exists(void)
{
  LOG_CLEAN("  test_terminal_action_exists...");

  block_sigchld();
  reap_children();

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

  unblock_sigchld();
}

/*
 * Test: terminal double init is safe
 */
void
test_terminal_double_init(void)
{
  LOG_CLEAN("  test_terminal_double_init...");

  block_sigchld();

  action_registry_init();
  terminal_init();
  terminal_init(); /* Should be safe */

  terminal_shutdown();
  action_registry_shutdown();

  unblock_sigchld();
}

/*
 * Test: terminal shutdown unregisters action
 */
void
test_terminal_shutdown(void)
{
  LOG_CLEAN("  test_terminal_shutdown...");

  block_sigchld();

  action_registry_init();
  terminal_init();

  /* Action should exist */
  assert(action_lookup("terminal.spawn") != NULL);

  terminal_shutdown();

  /* Action should be gone */
  assert(action_lookup("terminal.spawn") == NULL);

  action_registry_shutdown();

  unblock_sigchld();
}

/*
 * Test: default terminal command
 */
void
test_terminal_default_command(void)
{
  LOG_CLEAN("  test_terminal_default_command...");

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  /* Should return xterm or $TERMINAL env var */

  LOG_DEBUG("Default terminal: %s", cmd);
}

/*
 * Test: custom terminal command
 */
void
test_terminal_custom_command(void)
{
  LOG_CLEAN("  test_terminal_custom_command...");

  block_sigchld();

  action_registry_init();
  terminal_init();

  /* Save original TERMINAL env var if set */
  const char* orig_term = getenv("TERMINAL");
  char*        saved_term = NULL;
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

  /* Should fall back to default */
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

  unblock_sigchld();
}

/*
 * Test: terminal command with environment variable
 */
void
test_terminal_env_variable(void)
{
  LOG_CLEAN("  test_terminal_env_variable...");

  block_sigchld();

  /* Clear any custom command first */
  action_registry_init();
  terminal_init();
  terminal_set_command(NULL);

  /* Set TERMINAL env var */
  setenv("TERMINAL", "alacritty -e tmux", 1);

  /* Should return the env var value */
  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  assert(strcmp(cmd, "alacritty -e tmux") == 0);

  /* Clear env var */
  unsetenv("TERMINAL");

  terminal_shutdown();
  action_registry_shutdown();

  unblock_sigchld();
}

/*
 * Test: terminal action callback (mocked - doesn't actually spawn)
 */
void
test_terminal_action_callback(void)
{
  LOG_CLEAN("  test_terminal_action_callback...");

  block_sigchld();
  reap_children();

  action_registry_init();
  terminal_init();

  /* Verify action exists before calling */
  Action* action = action_lookup("terminal.spawn");
  assert(action != NULL);
  assert(action->callback == terminal_action_spawn);

  /* Test that terminal_spawn returns true (fork succeeds) */
  bool result = terminal_spawn();
  /* Result is true if fork succeeded (even if exec fails later) */
  assert(result == true);

  /* Reap the child (exec will fail if no matching terminal) */
  int status;
  pid_t waited = waitpid(-1, &status, WNOHANG);
  if (waited > 0) {
    /* Child exited - expected since no real terminal */
    LOG_DEBUG("Terminal child exited (expected - no real terminal installed)");
  }

  terminal_shutdown();
  action_registry_shutdown();

  unblock_sigchld();
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
  test_terminal_action_callback();

  LOG_CLEAN("  All terminal tests passed!");
}

TEST_GROUP(TerminalModule, {
  test_terminal_action_exists();
  test_terminal_double_init();
  test_terminal_shutdown();
  test_terminal_default_command();
  test_terminal_custom_command();
  test_terminal_env_variable();
  test_terminal_action_callback();
});