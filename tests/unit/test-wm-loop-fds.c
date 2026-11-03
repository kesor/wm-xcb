/*
 * test-wm-loop-fds.c - Tests for the descriptors the event loop watches
 *
 * A module that waits on a child process registers the read end of a pipe with
 * the loop instead of reading it from its own callback. The loop has to call the
 * handler when the descriptor is readable, has to stop when the handler is
 * removed, and has to survive a handler that removes itself: that is what a
 * finished child's pipe does.
 */

#include <stdbool.h>
#include <unistd.h>

#include "test-registry.h"
#include "test-wm.h"
#include "wm-xcb.h"

static int calls_self_removing = 0;
static int calls_other         = 0;
static int calls_idle          = 0;

static void
count_idle(int fd, void* userdata)
{
  (void) fd;
  (void) userdata;
  calls_idle++;
}

static void
remove_self(int fd, void* userdata)
{
  (void) userdata;
  calls_self_removing++;
  wm_loop_remove_fd_handler(fd);
}

static void
count_other(int fd, void* userdata)
{
  char byte;

  (void) read(fd, &byte, 1);
  (void) userdata;
  calls_other++;
}

void
test_loop_calls_handler_for_a_readable_descriptor(void)
{
  int fds[2];
  assert_or_abort(pipe(fds) == 0);

  assert(wm_loop_add_fd_handler(fds[0], count_idle, NULL));
  assert(wm_loop_dispatch_fd_handlers() == 0);
  assert(calls_idle == 0);

  assert(write(fds[1], "x", 1) == 1);
  assert(wm_loop_dispatch_fd_handlers() == 1);
  assert(calls_idle == 1);

  wm_loop_remove_fd_handler(fds[0]);
  /* Nothing is registered any more, so a readable descriptor is not called
     again even though its byte is still there. */
  assert(wm_loop_dispatch_fd_handlers() == 0);

  close(fds[0]);
  close(fds[1]);
}

void
test_loop_survives_a_handler_that_removes_itself(void)
{
  int first[2];
  int second[2];
  assert_or_abort(pipe(first) == 0);
  assert_or_abort(pipe(second) == 0);

  calls_self_removing = 0;
  calls_other         = 0;

  assert(wm_loop_add_fd_handler(first[0], remove_self, NULL));
  assert(wm_loop_add_fd_handler(second[0], count_other, NULL));

  assert(write(first[1], "x", 1) == 1);
  assert(write(second[1], "x", 1) == 1);

  assert(wm_loop_dispatch_fd_handlers() == 2);
  assert(calls_self_removing == 1);
  assert(calls_other == 1);

  /* The handler is gone: the descriptor is readable and nothing is called. */
  assert(wm_loop_dispatch_fd_handlers() == 0);

  wm_loop_remove_fd_handler(second[0]);

  close(first[0]);
  close(first[1]);
  close(second[0]);
  close(second[1]);
}

void
test_loop_refuses_a_descriptor_it_already_watches(void)
{
  int fds[2];
  assert_or_abort(pipe(fds) == 0);

  assert(wm_loop_add_fd_handler(fds[0], count_idle, NULL));
  assert(!wm_loop_add_fd_handler(fds[0], count_other, NULL));

  /* Removing a descriptor nobody watches is a no-op, not a crash. */
  wm_loop_remove_fd_handler(fds[0]);
  wm_loop_remove_fd_handler(fds[0]);

  close(fds[0]);
  close(fds[1]);
}

TEST_GROUP(LoopFds, {
  test_loop_calls_handler_for_a_readable_descriptor();
  test_loop_survives_a_handler_that_removes_itself();
  test_loop_refuses_a_descriptor_it_already_watches();
});
