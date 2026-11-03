/*
 * test-launcher.c - Unit tests for launcher component
 *
 * The launcher forks dmenu and hands the pipe to the event loop. These tests
 * replace dmenu with a script, so they can say what dmenu does without a
 * display: stay open, print a selection, or print nothing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include "src/actions/action-registry.h"
#include "src/actions/launcher.h"
#include "test-launcher.h"
#include "test-registry.h"
#include "wm-hub.h"
#include "wm-xcb.h"

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
 * A directory this test puts its own dmenu_run in, and the PATH it replaced.
 */
static char fake_bin[512]    = { 0 };
static char saved_path[4096] = { 0 };
static bool path_saved       = false;

static bool
fake_bin_ready(void)
{
  if (fake_bin[0] != '\0')
    return true;

  const char* tmp = getenv("TMPDIR");
  snprintf(fake_bin, sizeof fake_bin, "%s/wm-launcher-test-XXXXXX",
           tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");

  if (mkdtemp(fake_bin) == NULL) {
    fake_bin[0] = '\0';
    return false;
  }

  const char* path = getenv("PATH");
  snprintf(saved_path, sizeof saved_path, "%s", path != NULL ? path : "");
  path_saved = true;
  return true;
}

/* Writes an executable script into the fake bin directory. */
static bool
write_fake_script(const char* name, const char* body)
{
  char  file[600];
  FILE* script;

  if (!fake_bin_ready())
    return false;

  snprintf(file, sizeof file, "%s/%s", fake_bin, name);
  script = fopen(file, "w");
  if (script == NULL)
    return false;

  fputs("#!/bin/sh\n", script);
  fputs(body, script);
  fclose(script);

  return chmod(file, 0755) == 0;
}

/*
 * Puts a dmenu_run that runs `body` at the front of PATH.
 *
 * Every install gets its own directory. Overwriting a script a child is still
 * running from fails with ETXTBSY, and the earlier test's dmenu is still open
 * when the next test starts.
 */
static bool
install_fake_dmenu(const char* body)
{
  static int installs = 0;
  char       dir[620];
  char       file[700];
  FILE*      script;

  if (!fake_bin_ready())
    return false;

  snprintf(dir, sizeof dir, "%s/dmenu-%d", fake_bin, installs);
  installs++;

  if (mkdir(dir, 0700) != 0)
    return false;

  snprintf(file, sizeof file, "%s/dmenu_run", dir);
  script = fopen(file, "w");
  if (script == NULL)
    return false;

  fputs("#!/bin/sh\n", script);
  fputs(body, script);
  fclose(script);

  if (chmod(file, 0755) != 0)
    return false;

  size_t with_fake_length = strlen(dir) + strlen(saved_path) + 2;
  char*  with_fake        = malloc(with_fake_length);
  if (with_fake == NULL)
    return false;

  snprintf(with_fake, with_fake_length, "%s:%s", dir, saved_path);
  bool installed = setenv("PATH", with_fake, 1) == 0;
  free(with_fake);
  return installed;
}

static void
restore_real_path(void)
{
  if (path_saved)
    setenv("PATH", saved_path, 1);
}

/* Runs the loop's descriptor callbacks until the launcher is done with dmenu. */
static bool
drive_loop_until_launcher_finishes(int timeout_ms)
{
  for (int waited = 0; waited < timeout_ms; waited += 5) {
    wm_loop_dispatch_fd_handlers();
    if (!launcher_is_running())
      return true;
    usleep(5000);
  }
  return !launcher_is_running();
}

static long
elapsed_ms(const struct timespec* start, const struct timespec* end)
{
  return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_nsec - start->tv_nsec) / 1000000;
}

static int launcher_failures = 0;

static void
count_launcher_failure(struct Event event)
{
  (void) event;
  launcher_failures++;
}

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
  }
}

/*
 * Test: launcher action callback is correctly set
 */
static void
test_launcher_action_callback(void)
{
  action_registry_init();
  launcher_init();

  Action* action = action_lookup("app.launch");
  ASSERT(action != NULL, "app.launch action should exist");

  if (action != NULL) {
    /* Verify callback is set (but don't invoke it - dmenu is interactive) */
    ASSERT(action->callback != NULL, "action callback should be set");
    ASSERT(action->target_type == ACTION_TARGET_NONE,
           "action target type should be ACTION_TARGET_NONE");
  }

  launcher_shutdown();
  action_registry_shutdown();
}

/*
 * Test: the action returns while dmenu is still open
 *
 * A dmenu that sleeps and writes nothing is what the user looking at the prompt
 * looks like to the WM. If the action waited for it, this test would take as
 * long as the sleep.
 */
static void
test_launcher_returns_before_dmenu_exits(void)
{
  action_registry_init();
  hub_init();
  launcher_init();
  ASSERT(install_fake_dmenu("sleep 2\n"), "fake dmenu should be installed");

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  bool started = launcher_action_launch(NULL);
  clock_gettime(CLOCK_MONOTONIC, &end);

  ASSERT(started, "the action should report that dmenu started");
  ASSERT(launcher_is_running(), "dmenu should still be open when the action returns");
  long took = elapsed_ms(&start, &end);
  if (took >= 500)
    fprintf(stderr, "FAIL: launcher_action_launch took %ld ms\n", took);
  ASSERT(took < 500, "the action must not wait for dmenu");

  /* A second request while the prompt is open would fight over the keyboard. */
  ASSERT(!launcher_action_launch(NULL), "a second launch while dmenu is open should be refused");
  ASSERT(launcher_is_running(), "the second request should leave the open dmenu alone");

  launcher_shutdown();
  ASSERT(!launcher_is_running(), "shutdown should stop watching dmenu");
  hub_shutdown();
  action_registry_shutdown();
  restore_real_path();
}

/*
 * Test: a selection reaches the loop and runs the command
 */
static void
test_launcher_selection_starts_the_command(void)
{
  char marker[600];
  char runner[600];
  char dmenu[700];

  ASSERT(fake_bin_ready(), "fake bin directory should exist");

  snprintf(marker, sizeof marker, "%s/selected", fake_bin);
  snprintf(runner, sizeof runner, "%s/selected-runner", fake_bin);
  unlink(marker);

  snprintf(dmenu, sizeof dmenu, "touch %s\n", marker);
  ASSERT(write_fake_script("selected-runner", dmenu), "runner script should be written");

  snprintf(dmenu, sizeof dmenu, "echo %s\n", runner);
  ASSERT(install_fake_dmenu(dmenu), "fake dmenu should print the selection");

  action_registry_init();
  hub_init();
  launcher_init();

  launcher_failures = 0;
  hub_subscribe(EVT_LAUNCHER_FAILED, count_launcher_failure, NULL);

  ASSERT(launcher_action_launch(NULL), "the action should report that dmenu started");
  ASSERT(drive_loop_until_launcher_finishes(2000), "the loop should read the selection");

  bool selected = false;
  for (int waited = 0; waited < 2000; waited += 10) {
    if (access(marker, F_OK) == 0) {
      selected = true;
      break;
    }
    usleep(10000);
  }

  ASSERT(selected, "the selected command should have run");
  ASSERT(launcher_failures == 0, "a selection is not a failure");

  launcher_shutdown();
  hub_unsubscribe(EVT_LAUNCHER_FAILED, count_launcher_failure);
  hub_shutdown();
  action_registry_shutdown();
  restore_real_path();
  unlink(marker);
  unlink(runner);
}

/*
 * Test: dmenu that writes nothing reports a failure event
 */
static void
test_launcher_no_selection_emits_failure(void)
{
  ASSERT(install_fake_dmenu("exit 1\n"), "fake dmenu should exit without a selection");

  action_registry_init();
  hub_init();
  launcher_init();

  launcher_failures = 0;
  hub_subscribe(EVT_LAUNCHER_FAILED, count_launcher_failure, NULL);

  ASSERT(launcher_action_launch(NULL), "the action should report that dmenu started");
  ASSERT(drive_loop_until_launcher_finishes(2000), "the loop should see dmenu close its pipe");

  ASSERT(launcher_failures == 1, "a cancelled prompt should emit exactly one failure event");

  launcher_shutdown();
  hub_unsubscribe(EVT_LAUNCHER_FAILED, count_launcher_failure);
  hub_shutdown();
  action_registry_shutdown();
  restore_real_path();
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
  RUN_TEST(test_launcher_action_callback);
  RUN_TEST(test_launcher_returns_before_dmenu_exits);
  RUN_TEST(test_launcher_selection_starts_the_command);
  RUN_TEST(test_launcher_no_selection_emits_failure);

  printf("\n  [PASS] %d passed, [FAIL] %d failed\n\n", pass_count, fail_count);
}

/*
 * Register launcher tests
 */
TEST_GROUP(Launcher, {
  run_launcher_tests();
});
