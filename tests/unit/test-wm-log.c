/*
 * test-wm-log.c - Tests for the logger
 *
 * Tests:
 * - a logged line has reached the file before the caller continues
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wm-log.h"

#include "test-registry.h"

/*
 * Test: logger flushes the stream it wrote to
 *
 * A redirected stdout is fully buffered, and the WM is killed rather than
 * exited, so a line that only reaches the buffer never reaches the file: the
 * log then stops at the last buffer boundary, which reads as if the events
 * after it never happened.
 */
void
test_logger_flushes_each_line(void)
{
  static const char line[] = "logger flush test";
  char              path[] = "/tmp/wm-log-test-XXXXXX";

  LOG_CLEAN("  test_logger_flushes_each_line...");

  int fd = mkstemp(path);
  assert(fd >= 0);
  unlink(path);

  /* A terminal is line buffered, so a missing flush is invisible there; full
     buffering is what a log file gets and what the test has to reproduce.
     Changing the mode after earlier output is what glibc defines it to do: the
     pending output is flushed and the stream switches buffers. */
  fflush(stdout);
  int saved = dup(STDOUT_FILENO);
  assert(saved >= 0);
  int buffered = setvbuf(stdout, NULL, _IOFBF, BUFSIZ);
  assert(buffered == 0);
  int redirected = dup2(fd, STDOUT_FILENO);
  assert(redirected == STDOUT_FILENO);

  logger(stdout, "%s", line);

  off_t written = lseek(fd, 0, SEEK_END);

  int restored = dup2(saved, STDOUT_FILENO);
  assert(restored == STDOUT_FILENO);
  close(saved);
  setvbuf(stdout, NULL, _IOLBF, 0);
  close(fd);

  assert(written == (off_t) (strlen(line) + 1));
}

TEST_GROUP(Logger, {
  test_logger_flushes_each_line();
});
