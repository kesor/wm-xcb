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
 * - draining the pipe leaves it empty, so the next poll(2) waits again
 * - a second setup_signals() keeps the pipe the handlers already write to
 */

#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include "test-registry.h"
#include "test-wm.h"
#include "wm-running.h"
#include "wm-signals.h"

static int
is_readable(int fd, int timeout_ms)
{
  struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };

  return poll(&pfd, 1, timeout_ms) > 0;
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
  signals_drain_wakeups();
  assert(raise(SIGTERM) == 0);
  assert(is_readable(first, 100));
  signals_drain_wakeups();

  running = 1;
}

TEST_GROUP(SignalWakeup, {
  test_setup_signals_creates_a_nonblocking_wakeup_fd();
  test_sigterm_sets_running_and_wakes_the_loop();
  test_a_second_setup_keeps_the_same_pipe();
});
