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
#include "src/core/wm-hub.h"
#include "src/core/wm-xcb.h"
#include "test-launcher.h"
#include "test-registry.h"

/*
 * The shared counters, so a failed assertion here fails `make test`. A private
 * pair would print a failure and still let the run exit zero.
 */
#define ASSERT(cond, msg)                                             \
  do {                                                                \
    if (cond) {                                                       \
      tests_passed++;                                                 \
    } else {                                                          \
      tests_failed++;                                                 \
      fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    }                                                                 \
  } while (0)

#define RUN_TEST(fn)          \
  do {                        \
    printf("  " #fn "...\n"); \
    fn();                     \
  } while (0)

/*
 * A directory this test puts its own dmenu in, and the PATH it replaced.
 */
static char  fake_bin[512] = { 0 };
static char* saved_path    = NULL;

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

  /* Copied, not held in a fixed buffer: this environment's PATH is longer than
     any reasonable buffer, and a truncated copy silently hides `sh`. */
  const char* path = getenv("PATH");
  saved_path       = strdup(path != NULL ? path : "");
  if (saved_path == NULL) {
    fake_bin[0] = '\0';
    return false;
  }
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
 * Writes an executable script named `name` into `dir`, with `models` as its
 * second line and `cited` as its third.
 *
 * `models` names the program of the outside world this script stands in for and
 * says what that program does; `cited` is a line of that program, as installed,
 * introduced by its name. tests/fakes carries the same two lines for the
 * harnesses, and scripts/check-fakes.sh checks both. A stand-in that makes no
 * claim is a fake of nothing in particular, which is how this file's fakes came
 * to model a program the WM never execs while every suite stayed green.
 */
static bool
write_script_in(const char* dir, const char* name, const char* models, const char* cited, const char* body)
{
  char  file[700];
  FILE* script;

  snprintf(file, sizeof file, "%s/%s", dir, name);
  script = fopen(file, "w");
  if (script == NULL)
    return false;

  fputs("#!/bin/sh\n# models: ", script);
  fputs(models, script);
  fputs("\n# from ", script);
  fputs(cited, script);
  fputc('\n', script);
  fputs(body, script);
  fclose(script);

  return chmod(file, 0755) == 0;
}

/*
 * What the real dmenu does, and a line of the installed program. The line is
 * what makes the claim checkable: a stand-in can only cite it after reading the
 * file, and an upgrade that removes it fails instead of leaving the fake on the
 * version before it.
 */
#define MODELS_DMENU      "dmenu, which reads the command list on stdin and writes the chosen line on stdout"
#define CITES_DMENU       "dmenu: usage: dmenu [-bfiv] [-l lines] [-p prompt] [-fn font] [-m monitor]"

/* What the real dmenu_path does: it produces that list and exits. */
#define MODELS_DMENU_PATH "dmenu_path, which writes the list of runnable commands to stdout"
#define CITES_DMENU_PATH  "dmenu_path: cache=\"$cachedir/dmenu_run\""

/* The directory the last install_fake_dmenu() put its scripts in. */
static char dmenu_install_dir[620] = { 0 };

/*
 * Puts a dmenu that runs `body` at the front of PATH, with its command list
 * source next to it.
 *
 * The launcher runs `dmenu_path | dmenu`, so both names have to resolve to this
 * directory; a `dmenu_run` here would never be executed. Every install gets its
 * own directory. Overwriting a script a child is still running from fails with
 * ETXTBSY, and the earlier test's dmenu is still open when the next test
 * starts.
 */
static bool
install_fake_dmenu(const char* body)
{
  static int installs = 0;
  char       dir[620];

  if (!fake_bin_ready())
    return false;

  snprintf(dir, sizeof dir, "%s/dmenu-%d", fake_bin, installs);
  installs++;

  if (mkdir(dir, 0700) != 0)
    return false;

  if (!write_script_in(dir, "dmenu", MODELS_DMENU, CITES_DMENU, body))
    return false;
  if (!write_script_in(dir, "dmenu_path", MODELS_DMENU_PATH, CITES_DMENU_PATH, ":\n"))
    return false;

  size_t with_fake_length = strlen(dir) + strlen(saved_path) + 2;
  char*  with_fake        = malloc(with_fake_length);
  if (with_fake == NULL)
    return false;

  snprintf(with_fake, with_fake_length, "%s:%s", dir, saved_path);
  bool installed = setenv("PATH", with_fake, 1) == 0;
  free(with_fake);

  if (installed) {
    snprintf(dmenu_install_dir, sizeof dmenu_install_dir, "%s", dir);
  }
  return installed;
}

/*
 * Puts the fake dmenu at the front of PATH and leaves the real dmenu_path
 * behind it, so a test can run one stage of the launcher's pipeline as the
 * program the dmenu package ships.
 */
static bool
install_fake_dmenu_with_real_list_source(const char* body)
{
  char path[700];

  if (!install_fake_dmenu(body))
    return false;

  snprintf(path, sizeof path, "%s/dmenu_path", dmenu_install_dir);
  return unlink(path) == 0;
}

/*
 * The claim a stand-in makes, read from the two lines after its shebang: the
 * program it names, and a line of that program introduced by its name. Both are
 * what scripts/check-fakes.sh requires of the harness fakes.
 */
static bool
fake_models_claim(const char* file, char* program, size_t program_size, char* cited, size_t cited_size)
{
  char        line[512];
  char        from[64];
  FILE*       script   = fopen(file, "r");
  const char* models   = NULL;
  const char* citation = NULL;

  if (script == NULL)
    return false;

  if (fgets(line, sizeof line, script) == NULL || fgets(line, sizeof line, script) == NULL ||
      strncmp(line, "# models: ", strlen("# models: ")) != 0) {
    fclose(script);
    return false;
  }
  models = line + strlen("# models: ");

  size_t length = strcspn(models, " \t\n");
  while (length > 0 && (models[length - 1] == ',' || models[length - 1] == ':' || models[length - 1] == ';'))
    length--;
  if (length == 0 || length >= program_size) {
    fclose(script);
    return false;
  }
  memcpy(program, models, length);
  program[length] = '\0';

  snprintf(from, sizeof from, "# from %s: ", program);
  if (fgets(line, sizeof line, script) == NULL || strncmp(line, from, strlen(from)) != 0) {
    fclose(script);
    return false;
  }
  citation = line + strlen(from);

  length = strlen(citation);
  while (length > 0 && (citation[length - 1] == '\n' || citation[length - 1] == '\r'))
    length--;
  if (length == 0 || length >= cited_size) {
    fclose(script);
    return false;
  }
  memcpy(cited, citation, length);
  cited[length] = '\0';
  fclose(script);
  return true;
}

/* Whether `needle` occurs anywhere in the file at `path`, binary included. */
static bool
file_contains(const char* path, const char* needle)
{
  char   buffer[4096 + 512];
  FILE*  file    = fopen(path, "rb");
  size_t carried = 0;
  size_t need    = strlen(needle);
  bool   found   = false;

  if (file == NULL || need == 0)
    return false;

  for (;;) {
    size_t got = fread(buffer + carried, 1, 4096, file);
    if (got == 0)
      break;

    size_t have = carried + got;

    /* memcmp, not strstr: a program is mostly NUL bytes and strings, and the
       cited line sits among them. */
    for (size_t i = 0; i + need <= have; i++) {
      if (memcmp(buffer + i, needle, need) == 0) {
        found = true;
        break;
      }
    }
    if (found)
      break;

    /* The needle may straddle two reads; keep the last need-1 bytes. */
    carried = need > 1 && have >= need - 1 ? need - 1 : 0;
    if (carried > 0)
      memmove(buffer, buffer + have - carried, carried);
  }

  fclose(file);
  return found;
}

/* Resolves `name` against `path` the way execvp would. */
static bool
resolve_on_path(const char* name, const char* path, char* out, size_t out_size)
{
  const char* cursor = path;

  while (cursor != NULL && *cursor != '\0') {
    size_t length  = strcspn(cursor, ":");
    int    written = snprintf(out, out_size, "%.*s/%s", (int) length, cursor, name);
    if (written > 0 && (size_t) written < out_size && access(out, X_OK) == 0)
      return true;

    cursor += length;
    if (*cursor == ':')
      cursor++;
  }

  return false;
}

static void
restore_real_path(void)
{
  if (saved_path != NULL)
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

  launcher_failures = 0;
  hub_subscribe(EVT_LAUNCHER_FAILED, count_launcher_failure, NULL);

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
  ASSERT(launcher_failures == 0, "refusing a second prompt is not a failed launch");

  launcher_shutdown();
  ASSERT(!launcher_is_running(), "shutdown should stop watching dmenu");
  hub_unsubscribe(EVT_LAUNCHER_FAILED, count_launcher_failure);
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

  snprintf(dmenu, sizeof dmenu, "echo run >> %s\n", marker);
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

  /* The runner records each start. dmenu_run would run the selection in its
     own shell and the WM would run it again; the pipe carries the line to the
     WM alone, so there is exactly one start. */
  char  runs[64] = { 0 };
  FILE* log      = fopen(marker, "r");
  if (log != NULL) {
    size_t read_bytes = fread(runs, 1, sizeof runs - 1, log);
    runs[read_bytes]  = '\0';
    fclose(log);
  }
  ASSERT(strcmp(runs, "run\n") == 0, "the selection should run exactly once");

  launcher_shutdown();
  hub_unsubscribe(EVT_LAUNCHER_FAILED, count_launcher_failure);
  hub_shutdown();
  action_registry_shutdown();
  restore_real_path();
  unlink(marker);
  unlink(runner);
}

/*
 * Test: dmenu runs on the display the WM connected to
 *
 * A menu on another display is invisible where the user is looking, and the WM
 * then waits for a selection that cannot arrive. dmenu opens its own X
 * connection, so the only thing that decides where it appears is DISPLAY in the
 * environment the child inherits. Here that environment says one display and
 * the WM's connection says another; the child must get the WM's.
 */
static void
test_launcher_dmenu_uses_the_wm_display(void)
{
  char marker[600];
  char dmenu[700];
  char seen[128] = { 0 };

  ASSERT(fake_bin_ready(), "fake bin directory should exist");

  snprintf(marker, sizeof marker, "%s/display-seen", fake_bin);
  unlink(marker);
  snprintf(dmenu, sizeof dmenu, "printf '%%s' \"$DISPLAY\" > %s\n", marker);
  ASSERT(install_fake_dmenu(dmenu), "fake dmenu should record its DISPLAY");

  char*       saved_display = NULL;
  const char* had_display   = getenv("DISPLAY");
  if (had_display != NULL)
    saved_display = strdup(had_display);

  setenv("DISPLAY", ":99", 1);
  wm_xcb_set_display_name_for_tests(":4242");

  action_registry_init();
  hub_init();
  launcher_init();

  ASSERT(launcher_action_launch(NULL), "the action should report that dmenu started");
  ASSERT(drive_loop_until_launcher_finishes(2000), "the loop should read the selection");

  FILE* log = fopen(marker, "r");
  ASSERT(log != NULL, "the fake dmenu should have written its DISPLAY");
  size_t read_bytes = fread(seen, 1, sizeof seen - 1, log);
  seen[read_bytes]  = '\0';
  fclose(log);

  ASSERT(strcmp(seen, ":4242") == 0, "dmenu must inherit the WM's display, not the environment's");

  launcher_shutdown();
  hub_shutdown();
  action_registry_shutdown();
  wm_xcb_set_display_name_for_tests(NULL);
  if (saved_display != NULL) {
    setenv("DISPLAY", saved_display, 1);
    free(saved_display);
  } else {
    unsetenv("DISPLAY");
  }
  restore_real_path();
  unlink(marker);
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
 * Test: the fake dmenu names the program it stands in for, cites a line of it,
 * and that program is on PATH
 *
 * A stand-in is a claim about a program these tests never run, and nothing read
 * the claim back. Three fakes in this tree modelled a program the WM does not
 * exec while every suite stayed green, because the fakes answered for a program
 * nobody compared with the one the launcher runs. This resolves the name behind
 * the fake, requires the resolved path to be a file, and requires the line the
 * fake cites to occur in that file: a stand-in for a program nobody has, or one
 * written against a version that is not installed, fails here instead of
 * passing as a model of something.
 */
static void
test_launcher_fake_names_the_program_it_models(void)
{
  char        script[700];
  char        program[64]   = { 0 };
  char        cited[256]    = { 0 };
  char        resolved[600] = { 0 };
  struct stat info;

  ASSERT(install_fake_dmenu("exit 0\n"), "fake dmenu should be installed");

  snprintf(script, sizeof script, "%s/dmenu", dmenu_install_dir);
  ASSERT(fake_models_claim(script, program, sizeof program, cited, sizeof cited),
         "the fake dmenu should name a program and cite a line of it");
  ASSERT(strcmp(program, "dmenu") == 0, "the fake dmenu should say which program it models");
  ASSERT(strcmp(cited, "usage: dmenu [-bfiv] [-l lines] [-p prompt] [-fn font] [-m monitor]") == 0,
         "the fake dmenu should cite dmenu's usage line");

  /* Resolved behind the fake: the claim is only worth something if a program
     nobody here wrote answers to that name. */
  ASSERT(resolve_on_path(program, saved_path, resolved, sizeof resolved),
         "the program the fake models should be on PATH, which is what makes the stand-in comparable with it");

  if (resolved[0] != '\0') {
    /* A regular file, not a directory entry that answers to the name: the cited
       line is looked for in its contents, and a directory has none. */
    ASSERT(stat(resolved, &info) == 0 && S_ISREG(info.st_mode),
           "the program the fake models should be a regular file, so the cited line can be read back from it");
    ASSERT(file_contains(resolved, cited),
           "the cited line should occur in the program the fake names: nothing else shows the stand-in was written against it");
    printf("       models %s = %s (cites: %s)\n", program, resolved, cited);
  }

  restore_real_path();
}

/*
 * Test: one stage of the launcher's pipeline runs as the program the dmenu
 * package ships
 *
 * The other cases stand in for both stages of `dmenu_path | dmenu`, so nothing
 * in the suite would notice the exec line naming a pipeline that no installed
 * program supports. dmenu_path needs no display: it walks PATH and writes the
 * list. This case runs it and has the fake dmenu count what arrives on its
 * stdin, so a list source that is missing, renamed or broken fails here rather
 * than passing as a fake of an idea. The fake dmenu is still the stand-in
 * because the pipe has to stay in this process; the installed dmenu's selection
 * is a different case, on a display, in the seam suite.
 */
static void
test_launcher_runs_the_real_list_source(void)
{
  char words[600];
  char marker[600];
  char runner[600];
  char runner_body[700];
  char dmenu[1000];
  char real_list_source[600] = { 0 };
  char cache_home[600];

  ASSERT(fake_bin_ready(), "fake bin directory should exist");

  /* A precondition, not a result: this case checks the installed list source,
     and a skip here counts as a pass in the runner while nothing was run. */
  ASSERT(resolve_on_path("dmenu_path", saved_path, real_list_source, sizeof real_list_source),
         "dmenu_path should be on PATH behind the fake: this case runs the installed one and cannot test a stand-in");
  if (real_list_source[0] == '\0')
    return;

  snprintf(words, sizeof words, "%s/list-bytes", fake_bin);
  snprintf(marker, sizeof marker, "%s/real-list-source-ran", fake_bin);
  snprintf(runner, sizeof runner, "%s/real-list-source-runner", fake_bin);
  snprintf(cache_home, sizeof cache_home, "%s/dmenu-cache", fake_bin);
  unlink(words);
  unlink(marker);

  snprintf(runner_body, sizeof runner_body, ": > %s\n", marker);
  ASSERT(write_fake_script("real-list-source-runner", runner_body), "runner script should be written");

  snprintf(dmenu, sizeof dmenu, "wc -c > %s\nprintf '%%s\\n' %s\n", words, runner);
  ASSERT(install_fake_dmenu_with_real_list_source(dmenu), "fake dmenu should be installed");

  /* dmenu_path keeps its cache under XDG_CACHE_HOME, which must not be the
     user's own: this case is not a reason to rewrite their command list. */
  char* saved_cache_home = getenv("XDG_CACHE_HOME") != NULL ? strdup(getenv("XDG_CACHE_HOME")) : NULL;
  setenv("XDG_CACHE_HOME", cache_home, 1);

  action_registry_init();
  hub_init();
  launcher_init();

  ASSERT(launcher_action_launch(NULL), "the action should start the pipeline");
  ASSERT(drive_loop_until_launcher_finishes(15000), "the loop should read the selection");

  bool ran = false;
  for (int waited = 0; waited < 2000; waited += 10) {
    if (access(marker, F_OK) == 0) {
      ran = true;
      break;
    }
    usleep(10000);
  }
  ASSERT(ran, "the command dmenu returned should have run");

  long  list_bytes = 0;
  FILE* log        = fopen(words, "r");
  if (log != NULL) {
    if (fscanf(log, "%ld", &list_bytes) != 1)
      list_bytes = 0;
    fclose(log);
  }
  ASSERT(list_bytes > 0, "the real dmenu_path should have written a command list to the prompt");

  launcher_shutdown();
  hub_shutdown();
  action_registry_shutdown();
  restore_real_path();
  if (saved_cache_home != NULL) {
    setenv("XDG_CACHE_HOME", saved_cache_home, 1);
    free(saved_cache_home);
  } else {
    unsetenv("XDG_CACHE_HOME");
  }
  unlink(words);
  unlink(marker);
  unlink(runner);
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
  RUN_TEST(test_launcher_dmenu_uses_the_wm_display);
  RUN_TEST(test_launcher_fake_names_the_program_it_models);
  RUN_TEST(test_launcher_runs_the_real_list_source);

  free(saved_path);
  saved_path = NULL;
}

/*
 * Register launcher tests
 */
TEST_GROUP(Launcher, {
  run_launcher_tests();
});
