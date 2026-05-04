/*
 * test-launcher.c - Unit tests for launcher component
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/actions/action-registry.h"
#include "src/actions/launcher.h"
#include "test-launcher.h"
#include "test-registry.h"

/*
 * Track test results
 */
static int pass_count = 0;
static int fail_count = 0;

#define ASSERT(cond, msg)                                             \
  do {                                                                \
    if (cond) {                                                       \
      pass_count++;                                                   \
    } else {                                                          \
      fail_count++;                                                   \
      fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    }                                                                 \
  } while (0)

#define RUN_TEST(fn)          \
  do {                        \
    printf("  " #fn "...\n"); \
    fn();                     \
  } while (0)

/*
 * Test: launcher can be initialized and action exists
 */
static void
test_launcher_action_exists(void)
{
  /* Initialize action registry and launcher first */
  action_registry_init();
  launcher_init();

  Action* action = action_lookup("app.launch");
  ASSERT(action != NULL, "app.launch action should be registered");

  if (action != NULL) {
    ASSERT(strcmp(action->name, "app.launch") == 0,
           "action name should be app.launch");
    ASSERT(action->target_type == ACTION_TARGET_NONE,
           "action target type should be ACTION_TARGET_NONE");
    ASSERT(action->target_required == false,
           "action target_required should be false");
    ASSERT(action->callback != NULL, "action callback should not be NULL");
  }
}

/*
 * Test: launcher can be initialized
 */
static void
test_launcher_init(void)
{
  /* Initialize action registry */
  action_registry_init();

  /* Initialize launcher */
  launcher_init();

  /* Action should be registered */
  ASSERT(action_exists("app.launch"), "app.launch should exist after init");

  /* Shutdown */
  launcher_shutdown();
  action_registry_shutdown();
}

/*
 * Test: launcher can be initialized twice safely
 */
static void
test_launcher_double_init(void)
{
  action_registry_init();
  launcher_init();
  launcher_init(); /* Second init should be safe */

  ASSERT(action_exists("app.launch"), "app.launch should exist after double init");

  launcher_shutdown();
  action_registry_shutdown();
}

/*
 * Test: launcher shutdown removes action
 */
static void
test_launcher_shutdown(void)
{
  action_registry_init();
  launcher_init();

  ASSERT(action_exists("app.launch"), "app.launch should exist before shutdown");

  launcher_shutdown();

  ASSERT(!action_exists("app.launch"), "app.launch should not exist after shutdown");

  action_registry_shutdown();
}

/*
 * Test: dmenu configuration getter
 */
static void
test_launcher_dmenu_config(void)
{
  const LauncherDmenuConfig* config = launcher_get_dmenu_config();
  ASSERT(config != NULL, "dmenu config should not be NULL");

  if (config != NULL) {
    ASSERT(strcmp(config->prompt, "Run: ") == 0, "default prompt should be 'Run: '");
    ASSERT(config->grab_keyboard == true, "grab_keyboard should be true by default");
  }
}

/*
 * Test: launcher action invocation
 */
static void
test_launcher_action_invoke(void)
{
  ActionInvocation inv = {
    .target         = TARGET_ID_NONE,
    .data           = NULL,
    .correlation_id = 0,
    .userdata       = NULL,
    .target_type    = ACTION_TARGET_NONE,
  };

  /* Note: This will actually try to run dmenu, which may fail
   * in a headless environment. We just check that the action exists
   * and returns a result (even if false due to missing dmenu). */
  action_registry_init();
  launcher_init();

  bool result = action_invoke("app.launch", &inv);
  /* Result can be true (if dmenu available and user selects something)
   * or false (if dmenu not available or no selection).
   * We just verify the action can be invoked. */
  (void) result;

  /* Cleanup */
  launcher_shutdown();
  action_registry_shutdown();
}

/*
 * Run all launcher tests
 */
static void
run_launcher_tests(void)
{
  printf("=== Launcher Tests ===\n");

  RUN_TEST(test_launcher_action_exists);
  RUN_TEST(test_launcher_init);
  RUN_TEST(test_launcher_double_init);
  RUN_TEST(test_launcher_shutdown);
  RUN_TEST(test_launcher_dmenu_config);
  RUN_TEST(test_launcher_action_invoke);

  printf("\n  [PASS] %d passed, [FAIL] %d failed\n\n", pass_count, fail_count);
}

/*
 * Register launcher tests
 */
TEST_GROUP(Launcher, {
  run_launcher_tests();
});