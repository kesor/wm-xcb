#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wm-log.h"
#include "wm-running.h"
#include "wm-signals.h"

#define WAKEUP_READ  0
#define WAKEUP_WRITE 1

/*
 * The wait in poll(2) ends on an X event or on a byte in this pipe. A handler
 * cannot change what poll(2) is waiting on, so it writes a byte instead: a
 * SIGTERM that landed between the loop's test of `running` and its poll() would
 * otherwise be missed, and the WM would be unstoppable.
 */
static int wakeup_pipe[2] = { -1, -1 };

static void
signal_wakeup(void)
{
  /* A signal handler must not disturb the errno of the code it interrupted. */
  int        saved_errno = errno;
  const char byte        = 'w';
  ssize_t    written     = write(wakeup_pipe[WAKEUP_WRITE], &byte, 1);
  (void) written; /* A full pipe already holds a wakeup, so EAGAIN is fine. */
  errno = saved_errno;
}

void
sigchld(int unused)
{
  /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
  if (signal(SIGCHLD, sigchld) == SIG_ERR)
    LOG_ERROR("cannot install SIGCHLD event handler:");
  while (0 < waitpid(-1, NULL, WNOHANG))
    ;
}

void
sigint(int unused)
{
  static const char newline = '\n';

  running = 0;

  /* Only async-signal-safe calls here. printf and LOG_DEBUG take the stdio
     lock, which the code this signal interrupted may already hold: the handler
     would deadlock inside fprintf and the WM would never stop. write(2) is
     safe, and the newline is what keeps a terminal prompt off the last log
     line after ^C. */
  ssize_t written = write(STDOUT_FILENO, &newline, 1);
  (void) written;

  signal_wakeup();
}

static void
wakeup_pipe_set_nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG_FATAL("cannot make the signal wakeup pipe non-blocking:");
    return;
  }

  /* A forked launcher or terminal must not inherit the pipe, or it holds the
     write end open and the loop can read a wakeup nobody sent. */
  flags = fcntl(fd, F_GETFD, 0);
  if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
    LOG_FATAL("cannot make the signal wakeup pipe close-on-exec:");
}

void
setup_signals()
{
  /* Called once per process, but a test or a restart may call it again: a
     second pipe would leave the handlers writing to the new one while the loop
     waits on the old one. */
  if (wakeup_pipe[WAKEUP_READ] < 0) {
    if (pipe(wakeup_pipe) < 0) {
      LOG_FATAL("cannot create the signal wakeup pipe:");
    } else {
      wakeup_pipe_set_nonblocking(wakeup_pipe[WAKEUP_READ]);
      wakeup_pipe_set_nonblocking(wakeup_pipe[WAKEUP_WRITE]);
    }
  }

  /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
  if (signal(SIGINT, sigint) == SIG_ERR)
    LOG_FATAL("cannot install SIGINT event handler");

  /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
  if (signal(SIGTERM, sigint) == SIG_ERR)
    LOG_FATAL("cannot install SIGTERM event handler");
}

int
signals_wakeup_fd(void)
{
  return wakeup_pipe[WAKEUP_READ];
}

void
signals_drain_wakeups(void)
{
  char buffer[64];

  if (wakeup_pipe[WAKEUP_READ] < 0)
    return;

  /* allow-blocking: the wakeup pipe is O_NONBLOCK, so this returns EAGAIN once
     it is empty */
  while (read(wakeup_pipe[WAKEUP_READ], buffer, sizeof(buffer)) > 0)
    ;
}
