/*
 * test-wm-xcb-ewmh.c - Tests for the EWMH connection holder
 *
 * Tests:
 * - destruct_ewmh() releases the per-screen lists and clears the global
 * - destruct_ewmh() is a no-op when the global is already unset
 */

#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "wm-xcb-ewmh.h"

#include "test-registry.h"
#include "test-wm.h"

/*
 * Test: destruct releases the per-screen lists init_atoms allocated
 *
 * A unit run has no X connection, so xcb_ewmh_init_atoms() cannot be called and
 * the two arrays it fills are allocated here instead. Dropping the wipe leaves
 * them unreachable, which the sanitize gate reports as a leak against this
 * function. The global must not be left pointing at the freed struct:
 * ewmh_check_wm_state_fullscreen() reads it.
 */
void
test_ewmh_destruct_releases_per_screen_lists(void)
{
  LOG_CLEAN("  test_ewmh_destruct_releases_per_screen_lists...");

  ewmh = calloc(1, sizeof(*ewmh));
  assert_or_abort(ewmh != NULL);
  ewmh->screens       = malloc(sizeof(*ewmh->screens));
  ewmh->_NET_WM_CM_Sn = malloc(sizeof(*ewmh->_NET_WM_CM_Sn));
  assert_or_abort(ewmh->screens != NULL);
  assert_or_abort(ewmh->_NET_WM_CM_Sn != NULL);

  destruct_ewmh();

  assert(ewmh == NULL);
}

/*
 * Test: a second destruct, or one before setup, does not touch the global
 *
 * xcb_ewmh_connection_wipe() dereferences its argument. free() did not, so
 * the old destruct was safe to call when ewmh was NULL and the new one is
 * not, unless it returns first.
 */
void
test_ewmh_destruct_when_unset_is_a_no_op(void)
{
  LOG_CLEAN("  test_ewmh_destruct_when_unset_is_a_no_op...");

  ewmh = NULL;
  destruct_ewmh();
  assert(ewmh == NULL);
}

TEST_GROUP(Ewmh, {
  test_ewmh_destruct_releases_per_screen_lists();
  test_ewmh_destruct_when_unset_is_a_no_op();
});
