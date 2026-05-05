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
#include <string.h>

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

  LOG_CLEAN("  test_terminal_action_callback...");
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

  const char* cmd = terminal_get_default();
  assert(cmd != NULL);
  /* Should return xterm or $TERMINAL env var */

  LOG_DEBUG("Default terminal: %s", cmd);
}

/*
 * Test: terminal action callback
 */
void
test_terminal_action_callback(void)
{
  LOG_CLEAN("  test_terminal_action_callback...");

  action_registry_init();
  terminal_init();

  /* Invoke the action directly */
  ActionInvocation inv = {
    .target         = TARGET_ID_NONE,
    .data           = NULL,
    .correlation_id = 0,
    .userdata       = NULL,
  };

  bool result = action_invoke("terminal.spawn", &inv);
  /* Result is true if fork succeeded (even if exec fails later) */
  assert(result == true);

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
  test_terminal_action_callback();

  LOG_CLEAN("  All terminal tests passed!");
}

TEST_GROUP(TerminalModule, {
  test_terminal_action_exists();
  test_terminal_double_init();
  test_terminal_shutdown();
  test_terminal_default_command();
  test_terminal_action_callback();
});