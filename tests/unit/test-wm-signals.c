/*
 * test-wm-signals.c - Tests for the signal wakeup pipe
 *
 * A loop that blocks in poll(2) only returns to check `running` when the wait
 * ends. SIGTERM has to end it, so the handler writes a byte to a pipe that the
 * wait includes.
 *
 * Tests:
 * - installing the handlers creates a non-blocking wakeup fd
 * - SIGTERM clears `running` and makes the wakeup fd readable
 * - SIGHUP records a reload request and leaves `running` alone
 * - draining the pipe leaves it empty, so the next poll(2) waits again
 * - a second setup_signals() keeps the pipe the handlers already write to
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <sys/wait.h>

#include "src/core/wm-running.h"
#include "src/core/wm-signals.h"
#include "tests/unit/test-registry.h"
#include "tests/unit/test-wm.h"

static void (*previous_sigint)(int);
static void (*previous_sigterm)(int);
static void (*previous_sighup)(int);
static void (*previous_sigchld)(int);

static int
is_readable(int fd, int timeout_ms)
{
  struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };

  return poll(&pfd, 1, timeout_ms) > 0;
}

static void
remember_signal_dispositions(void)
{
  /* setup_signals() replaces these for the whole process. Leaving SIGTERM
     caught makes the test binary ignore the signal used to stop a hung run. */
  previous_sigint = signal(SIGINT, SIG_DFL);
  signal(SIGINT, previous_sigint);
  previous_sigterm = signal(SIGTERM, SIG_DFL);
  signal(SIGTERM, previous_sigterm);
  previous_sighup = signal(SIGHUP, SIG_DFL);
  signal(SIGHUP, previous_sighup);
  previous_sigchld = signal(SIGCHLD, SIG_DFL);
  signal(SIGCHLD, previous_sigchld);
}

static void
restore_signal_dispositions(void)
{
  signal(SIGINT, previous_sigint);
  signal(SIGTERM, previous_sigterm);
  signal(SIGHUP, previous_sighup);
  signal(SIGCHLD, previous_sigchld);
}

void
test_setup_signals_creates_a_nonblocking_wakeup_fd(void)
{
  LOG_CLEAN("  test_setup_signals_creates_a_nonblocking_wakeup_fd...");

  setup_signals();

  int fd = signals_wakeup_fd();
  assert_or_abort(fd >= 0);
  assert(!is_readable(fd, 0));

  int flags = fcntl(fd, F_GETFL, 0);
  assert(flags >= 0);
  assert((flags & O_NONBLOCK) != 0);

  flags = fcntl(fd, F_GETFD, 0);
  assert(flags >= 0);
  assert((flags & FD_CLOEXEC) != 0);
}

void
test_sigterm_sets_running_and_wakes_the_loop(void)
{
  LOG_CLEAN("  test_sigterm_sets_running_and_wakes_the_loop...");

  sig_atomic_t was_running = running;

  setup_signals();
  int fd = signals_wakeup_fd();
  assert_or_abort(fd >= 0);

  signals_drain_wakeups();
  running = 1;

  assert(raise(SIGTERM) == 0);
  assert(running == 0);
  assert(is_readable(fd, 100));

  signals_drain_wakeups();
  assert(!is_readable(fd, 0));

  running = was_running;
}

void
test_a_second_setup_keeps_the_same_pipe(void)
{
  LOG_CLEAN("  test_a_second_setup_keeps_the_same_pipe...");

  setup_signals();
  int first = signals_wakeup_fd();
  assert_or_abort(first >= 0);

  setup_signals();
  assert(signals_wakeup_fd() == first);

  /* The handler still writes to the fd the loop waits on. */
  sig_atomic_t was_running = running;

  signals_drain_wakeups();
  assert(raise(SIGTERM) == 0);
  assert(is_readable(first, 100));
  signals_drain_wakeups();

  running = was_running;
}

/*
 * A forked child is reaped by the handler, not by the code that spawned it:
 * that is what lets the launcher and the terminal return before their child
 * exits. Nothing in the WM calls waitpid() for a child it started.
 */
void
test_sighup_asks_for_a_reload_and_keeps_running(void)
{
  LOG_CLEAN("  test_sighup_asks_for_a_reload_and_keeps_running...");

  setup_signals();
  int fd = signals_wakeup_fd();
  assert_or_abort(fd >= 0);

  signals_drain_wakeups();
  (void) signals_take_reload_request();

  sig_atomic_t was_running = running;
  running                  = 1;

  assert(raise(SIGHUP) == 0);

  /* The default action for SIGHUP is to end the process: a WM that dies on a
     terminal's hangup has not reloaded anything. */
  assert(running == 1);
  assert(is_readable(fd, 100));

  /* Reading the request clears it, so the loop reloads once per signal. */
  assert(signals_take_reload_request() == true);
  assert(signals_take_reload_request() == false);

  signals_drain_wakeups();
  running = was_running;
}

void
test_sigchld_reaps_a_child_without_a_wait(void)
{
  LOG_CLEAN("  test_sigchld_reaps_a_child_without_a_wait...");

  setup_signals();

  pid_t pid = fork();
  assert_or_abort(pid >= 0);

  if (pid == 0)
    _exit(0);

  /* The handler runs on delivery; kill(2) says ESRCH once the child has been
     collected and is no longer a zombie. */
  bool reaped = false;
  for (int waited = 0; waited < 2000; waited += 10) {
    if (kill(pid, 0) != 0 && errno == ESRCH) {
      reaped = true;
      break;
    }
    usleep(10000);
  }

  assert(reaped);
  errno = 0;
  assert(waitpid(pid, NULL, WNOHANG) == -1);
  assert(errno == ECHILD);
}

TEST_GROUP(SignalWakeup, {
  remember_signal_dispositions();
  test_setup_signals_creates_a_nonblocking_wakeup_fd();
  test_sigterm_sets_running_and_wakes_the_loop();
  test_a_second_setup_keeps_the_same_pipe();
  test_sighup_asks_for_a_reload_and_keeps_running();
  test_sigchld_reaps_a_child_without_a_wait();
  restore_signal_dispositions();
});
