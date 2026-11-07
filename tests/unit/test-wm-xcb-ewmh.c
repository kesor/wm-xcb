/*
 * test-wm-xcb-ewmh.c - Tests for the EWMH connection holder and the property
 * reads the WM does through it
 *
 * Tests:
 * - destruct_ewmh() releases the per-screen lists and clears the global
 * - destruct_ewmh() is a no-op when the global is already unset
 * - a _NET_WM_STATE reply is read for the fullscreen atom
 */

#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "src/core/wm-xcb-ewmh.h"

#include "test-registry.h"
#include "test-wm.h"

/*
 * Test: destruct releases the per-screen lists init_atoms allocated
 *
 * A unit run has no X connection, so xcb_ewmh_init_atoms() cannot be called and
 * the two arrays it fills are allocated here instead. Dropping the wipe leaves
 * them unreachable, which the sanitize gate reports as a leak against this
 * function. The global must not be left pointing at the freed struct:
 * ewmh_wm_state_is_fullscreen() reads it.
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

/*
 * A fake _NET_WM_STATE reply.
 *
 * A unit run has no X connection, so the reply is built here in the layout
 * libxcb hands to its accessors: the atoms follow the header, and value_len
 * counts them. xcb_get_property_value() is `(void*)(reply + 1)`, so the array
 * below has to sit directly after the header.
 */
typedef struct FakeWmStateReply {
  xcb_get_property_reply_t header;
  xcb_atom_t               atoms[2];
} FakeWmStateReply;

static xcb_get_property_reply_t*
fake_wm_state_reply(FakeWmStateReply* fake, uint32_t atom_count)
{
  memset(fake, 0, sizeof *fake);
  fake->header.format    = 32;
  fake->header.value_len = atom_count;
  return &fake->header;
}

/*
 * Test: the fullscreen atom anywhere in the list counts as fullscreen
 */
void
test_ewmh_wm_state_reply_finds_fullscreen(void)
{
  LOG_CLEAN("  test_ewmh_wm_state_reply_finds_fullscreen...");

  xcb_ewmh_connection_t conn;
  memset(&conn, 0, sizeof conn);
  conn._NET_WM_STATE_FULLSCREEN = 4242;

  FakeWmStateReply fake;

  /* First entry. */
  xcb_get_property_reply_t* reply = fake_wm_state_reply(&fake, 2);
  fake.atoms[0]                   = 4242;
  fake.atoms[1]                   = 7;
  assert(ewmh_wm_state_is_fullscreen(&conn, reply));

  /* Second entry: the loop has to walk the whole list. */
  reply         = fake_wm_state_reply(&fake, 2);
  fake.atoms[0] = 7;
  fake.atoms[1] = 4242;
  assert(ewmh_wm_state_is_fullscreen(&conn, reply));
}

/*
 * Test: a list without the fullscreen atom, an empty list, and a reply that is
 * not a 32-bit list are all "not fullscreen" rather than an error
 */
void
test_ewmh_wm_state_reply_without_fullscreen(void)
{
  LOG_CLEAN("  test_ewmh_wm_state_reply_without_fullscreen...");

  xcb_ewmh_connection_t conn;
  memset(&conn, 0, sizeof conn);
  conn._NET_WM_STATE_FULLSCREEN = 4242;

  FakeWmStateReply fake;

  xcb_get_property_reply_t* reply = fake_wm_state_reply(&fake, 2);
  fake.atoms[0]                   = 7;
  fake.atoms[1]                   = 8;
  assert(!ewmh_wm_state_is_fullscreen(&conn, reply));

  /* An empty list has no atom to match. */
  reply = fake_wm_state_reply(&fake, 0);
  assert(!ewmh_wm_state_is_fullscreen(&conn, reply));

  /* A property that is not a list of CARD32 atoms is not a state list. */
  reply              = fake_wm_state_reply(&fake, 2);
  fake.header.format = 8;
  fake.atoms[0]      = 4242;
  assert(!ewmh_wm_state_is_fullscreen(&conn, reply));

  /* No reply at all, and no EWMH connection: nothing to read. */
  assert(!ewmh_wm_state_is_fullscreen(&conn, NULL));
  assert(!ewmh_wm_state_is_fullscreen(NULL, reply));
}

TEST_GROUP(Ewmh, {
  test_ewmh_destruct_releases_per_screen_lists();
  test_ewmh_destruct_when_unset_is_a_no_op();
  test_ewmh_wm_state_reply_finds_fullscreen();
  test_ewmh_wm_state_reply_without_fullscreen();
});
