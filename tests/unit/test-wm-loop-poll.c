/*
 * test-wm-loop-poll.c - Tests for the callbacks the event loop runs every
 * iteration
 *
 * A module that sent a cookie reads its reply from one of these callbacks
 * instead of waiting for it inside its XCB handler. The loop has to run them
 * once per iteration, stop when one is removed, and survive a callback that
 * removes itself: a module that has its answer does exactly that.
 */

#include <stdbool.h>

#include "src/core/wm-xcb.h"
#include "test-registry.h"
#include "test-wm.h"

static int calls_self_removing = 0;
static int calls_other         = 0;
static int calls_kept          = 0;

static void
count_and_keep(void* userdata)
{
  (void) userdata;
  calls_kept++;
}

static void
remove_self(void* userdata)
{
  calls_self_removing++;
  wm_loop_remove_poll_handler(remove_self, userdata);
}

static void
count_other(void* userdata)
{
  (void) userdata;
  calls_other++;
}

void
test_loop_poll_runs_the_registered_callback(void)
{
  calls_kept = 0;

  assert(wm_loop_add_poll_handler(count_and_keep, NULL));
  assert(wm_loop_dispatch_poll_handlers() == 1);
  assert(calls_kept == 1);

  /* A poll callback stays registered: it is run every iteration until the
     module has its answer and removes it. */
  assert(wm_loop_dispatch_poll_handlers() == 1);
  assert(calls_kept == 2);

  wm_loop_remove_poll_handler(count_and_keep, NULL);
  assert(wm_loop_dispatch_poll_handlers() == 0);
}

void
test_loop_poll_survives_a_callback_that_removes_itself(void)
{
  calls_self_removing = 0;
  calls_other         = 0;

  assert(wm_loop_add_poll_handler(remove_self, NULL));
  assert(wm_loop_add_poll_handler(count_other, NULL));

  assert(wm_loop_dispatch_poll_handlers() == 2);
  assert(calls_self_removing == 1);
  assert(calls_other == 1);

  assert(wm_loop_dispatch_poll_handlers() == 1);
  assert(calls_self_removing == 1);
  assert(calls_other == 2);

  wm_loop_remove_poll_handler(count_other, NULL);
}

void
test_loop_poll_refuses_a_duplicate_and_ignores_removal_of_an_unknown(void)
{
  assert(wm_loop_add_poll_handler(count_other, NULL));
  assert(!wm_loop_add_poll_handler(count_other, NULL));

  /* A second registration with a different userdata is a different poll. */
  assert(wm_loop_add_poll_handler(count_other, (void*) 1));

  /* Removing something nobody registered is a no-op, not a crash. */
  wm_loop_remove_poll_handler(count_other, (void*) 2);

  wm_loop_remove_poll_handler(count_other, NULL);
  wm_loop_remove_poll_handler(count_other, (void*) 1);

  assert(wm_loop_dispatch_poll_handlers() == 0);
}

TEST_GROUP(LoopPoll, {
  test_loop_poll_runs_the_registered_callback();
  test_loop_poll_survives_a_callback_that_removes_itself();
  test_loop_poll_refuses_a_duplicate_and_ignores_removal_of_an_unknown();
});
