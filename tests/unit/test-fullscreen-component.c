/*
 * Fullscreen Component Tests
 *
 * Covers the action the default config binds to MOD+f: that the name is
 * registered with a client target, and that invoking it moves the focused
 * client's fullscreen state machine.
 */

#include "tests/unit/test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <stdlib.h>
#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-xcb-ewmh.h"
#include "src/sm/sm-registry.h"
#include "src/xcb/xcb-handler.h"
#include "tests/unit/test-wm.h"

/*
 * Test: fullscreen.toggle is registered against the focused client.
 */
void
test_fullscreen_action_registered(void)
{
  LOG_CLEAN("== Testing fullscreen action registration");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  fullscreen_component_init();

  Action* action = action_lookup("fullscreen.toggle");
  assert(action != NULL);
  if (action != NULL) {
    assert(action->callback != NULL);
    assert(action->target_type == ACTION_TARGET_CLIENT);
    assert(action->target_required == true);
  }

  fullscreen_component_shutdown();

  /* Shutdown must take the name with it, or a restart cannot register it */
  assert(action_lookup("fullscreen.toggle") == NULL);

  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: invoking the action toggles the client the focus component resolved.
 */
void
test_fullscreen_toggle_action(void)
{
  LOG_CLEAN("== Testing fullscreen.toggle action");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  focus_component_init();
  fullscreen_component_init();

  Client* c = client_create(700);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  client_set_focusable(c, true);
  client_set_mapped(c, true);
  focus_set_state(c, FOCUS_STATE_FOCUSED);

  assert(fullscreen_is_fullscreen(c) == false);

  assert(action_invoke("fullscreen.toggle", NULL) == true);
  assert(fullscreen_is_fullscreen(c) == true);

  assert(action_invoke("fullscreen.toggle", NULL) == true);
  assert(fullscreen_is_fullscreen(c) == false);

  /* With nothing focused the action has no target and must not pretend. */
  focus_set_state(c, FOCUS_STATE_UNFOCUSED);
  assert(action_invoke("fullscreen.toggle", NULL) == false);

  /* Clients own SMs that point at the component's template, so they go first */
  client_list_shutdown();
  fullscreen_component_shutdown();
  focus_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: registering the action is part of init. Without the registry the
 * component must not come up half-initialized, or a caller that runs before
 * keybinding_init drops fullscreen for the whole session.
 */
void
test_fullscreen_init_requires_action_registry(void)
{
  LOG_CLEAN("== Testing fullscreen init without the action registry");

  hub_init();
  sm_registry_init();
  xcb_handler_init();

  assert(fullscreen_component_init() == false);
  assert(fullscreen_component_is_initialized() == false);
  assert(hub_get_component_by_name(FULLSCREEN_COMPONENT_NAME) == NULL);

  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

static size_t fullscreen_failures = 0;

static void
count_fullscreen_failure(struct Event event)
{
  (void) event;
  fullscreen_failures++;
}

/*
 * Test: a request is intent, so the guard on the template's WINDOWED →
 * FULLSCREEN row decides. An unmanaged client has nothing to go fullscreen
 * into, so the request is refused and the failure is reported instead of the
 * state moving anyway.
 */
void
test_fullscreen_request_refused_for_unmanaged_client(void)
{
  LOG_CLEAN("== Testing a fullscreen request for an unmanaged client");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  fullscreen_component_init();

  fullscreen_failures = 0;
  hub_subscribe(EVT_FULLSCREEN_FAILED, count_fullscreen_failure, NULL);

  Client* c = client_create(701);
  assert_or_abort(c != NULL);
  assert(client_is_managed(c) == false);

  hub_send_request(REQ_CLIENT_FULLSCREEN, c->target.id);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);
  assert(fullscreen_failures == 1);

  /* The refusal is about the client, not the request: the same request is
     allowed once there is a managed window behind it. */
  client_set_managed(c, true);
  hub_send_request(REQ_CLIENT_FULLSCREEN, c->target.id);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);
  assert(fullscreen_failures == 1);

  client_list_shutdown();
  fullscreen_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Counters standing in for the template's actions. Swapping them in by the
 * names the template carries asks the question a reader of the table asks:
 * does a request reach the action column of the row it declared?
 */
static size_t  fake_enter_calls  = 0;
static size_t  fake_exit_calls   = 0;
static Client* fake_action_owner = NULL;

static bool
fake_enter_fullscreen(StateMachine* sm, void* data)
{
  (void) data;
  fake_enter_calls++;
  fake_action_owner = (Client*) sm_get_owner(sm);
  return true;
}

static bool
fake_exit_fullscreen(StateMachine* sm, void* data)
{
  (void) data;
  (void) sm;
  fake_exit_calls++;
  return true;
}

/*
 * Test: the request path runs the transition the template declares, including
 * its action. A refusal stops before the action, so counting the action is how
 * this test tells the guard and the action apart.
 */
void
test_fullscreen_request_runs_the_template_action(void)
{
  LOG_CLEAN("== Testing a fullscreen request runs the template action");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  fullscreen_component_init();

  sm_unregister_action("fs_action_enter_fullscreen");
  sm_register_action("fs_action_enter_fullscreen", fake_enter_fullscreen);
  sm_unregister_action("fs_action_exit_fullscreen");
  sm_register_action("fs_action_exit_fullscreen", fake_exit_fullscreen);

  Client* c = client_create(702);
  assert_or_abort(c != NULL);

  hub_send_request(REQ_CLIENT_FULLSCREEN, c->target.id);
  assert(fake_enter_calls == 0);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);

  client_set_managed(c, true);

  hub_send_request(REQ_CLIENT_FULLSCREEN, c->target.id);
  assert(fake_enter_calls == 1);
  assert(fake_action_owner == c);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  hub_send_request(REQ_CLIENT_FULLSCREEN, c->target.id);
  assert(fake_exit_calls == 1);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);

  /* Put the component's own actions back: the next test in the group re-inits
     the registry, and a swap left behind would make that init register a
     counter instead of the X write. */
  sm_unregister_action("fs_action_enter_fullscreen");
  sm_register_action("fs_action_enter_fullscreen", fullscreen_action_enter_fullscreen);
  sm_unregister_action("fs_action_exit_fullscreen");
  sm_register_action("fs_action_exit_fullscreen", fullscreen_action_exit_fullscreen);

  client_list_shutdown();
  fullscreen_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Fake reply source for the queue walk: a bit per cookie sequence says which
 * reply has arrived, and the value handed back is that sequence, so the test
 * can name the order the walk applied them in.
 */
typedef struct FakeReplies {
  uint32_t ready_mask;
  uint32_t applied[4];
  size_t   applied_count;
} FakeReplies;

#define REPLY_READY(sequence) (1u << (sequence))

static bool
fake_read_arrived(void*                     userdata,
                  xcb_get_property_cookie_t cookie,
                  xcb_window_t              window,
                  void**                    reply)
{
  FakeReplies* fake = (FakeReplies*) userdata;
  (void) window;

  if ((fake->ready_mask & REPLY_READY(cookie.sequence)) == 0)
    return false;

  uint32_t* sequence = malloc(sizeof *sequence);
  assert_or_abort(sequence != NULL);
  *sequence = cookie.sequence;
  *reply    = sequence;
  return true;
}

static void
fake_read_apply(void* userdata, xcb_window_t window, void* reply)
{
  FakeReplies* fake = (FakeReplies*) userdata;
  (void) window;

  assert_or_abort(fake->applied_count < 4);
  fake->applied[fake->applied_count++] = *(uint32_t*) reply;
  free(reply);
}

static xcb_get_property_cookie_t
fake_cookie(uint32_t sequence)
{
  xcb_get_property_cookie_t cookie = { .sequence = sequence };
  return cookie;
}

/*
 * Test: two replies ready in one iteration are applied oldest first. Applying
 * the newer one first leaves the state machine on the older answer.
 */
void
test_fullscreen_state_read_applies_oldest_first(void)
{
  LOG_CLEAN("== Testing _NET_WM_STATE replies apply oldest first");

  fullscreen_state_read_queue_reset_for_tests();

  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(1), 101));
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(2), 102));
  assert_or_abort(fullscreen_state_read_queue_count_for_tests() == 2);

  FakeReplies fake = { .ready_mask = REPLY_READY(1) | REPLY_READY(2) };
  size_t      applied =
      fullscreen_state_read_drain(fake_read_arrived, fake_read_apply, &fake);

  assert(applied == 2);
  assert(fake.applied_count == 2);
  assert(fake.applied[0] == 1);
  assert(fake.applied[1] == 2);
  assert(fullscreen_state_read_queue_count_for_tests() == 0);

  fullscreen_state_read_queue_reset_for_tests();
}

/*
 * Test: a head whose reply has not arrived stops the walk. The entries behind
 * it are later, so applying a ready one early is the same defect in a different
 * shape: it does not wait for the answer before it.
 */
void
test_fullscreen_state_read_stops_at_unarrived_head(void)
{
  LOG_CLEAN("== Testing an unarrived _NET_WM_STATE reply stops the walk");

  fullscreen_state_read_queue_reset_for_tests();

  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(1), 101));
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(2), 102));
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(3), 103));

  FakeReplies fake = { .ready_mask = REPLY_READY(2) | REPLY_READY(3) };
  size_t      applied =
      fullscreen_state_read_drain(fake_read_arrived, fake_read_apply, &fake);

  assert(applied == 0);
  assert(fake.applied_count == 0);
  assert(fullscreen_state_read_queue_count_for_tests() == 3);

  /* The head arrives: the walk resumes at the oldest and keeps the order. */
  fake.ready_mask = REPLY_READY(1) | REPLY_READY(2) | REPLY_READY(3);
  applied         = fullscreen_state_read_drain(fake_read_arrived, fake_read_apply, &fake);

  assert(applied == 3);
  assert(fake.applied_count == 3);
  assert(fake.applied[0] == 1);
  assert(fake.applied[1] == 2);
  assert(fake.applied[2] == 3);
  assert(fullscreen_state_read_queue_count_for_tests() == 0);

  fullscreen_state_read_queue_reset_for_tests();
}

/*
 * A fake _NET_WM_STATE reply. libxcb's accessors read the atoms at reply + 1,
 * and value_len counts them. The fullscreen atom is the one the fake connection
 * below names.
 */
#define TEST_FULLSCREEN_ATOM 4242u

typedef struct FakeWmStateReply {
  xcb_get_property_reply_t header;
  xcb_atom_t               atom;
} FakeWmStateReply;

static xcb_get_property_reply_t*
wm_state_reply(bool fullscreen)
{
  FakeWmStateReply* fake = calloc(1, sizeof *fake);
  assert_or_abort(fake != NULL);
  fake->header.format = 32;
  if (fullscreen) {
    fake->header.value_len = 1;
    fake->atom             = TEST_FULLSCREEN_ATOM;
  }
  return &fake->header;
}

typedef struct HandedReplies {
  xcb_get_property_reply_t* replies[2];
  size_t                    count;
  size_t                    next;
} HandedReplies;

static bool
hand_state_reply(void*                     userdata,
                 xcb_get_property_cookie_t cookie,
                 xcb_window_t              window,
                 void**                    reply)
{
  HandedReplies* handed = (HandedReplies*) userdata;
  (void) cookie;
  (void) window;

  if (handed->next >= handed->count)
    return false;

  *reply = handed->replies[handed->next++];
  return true;
}

static void
syncing_state_apply(void* userdata, xcb_window_t window, void* reply)
{
  (void) userdata;
  fullscreen_state_reply_apply_for_tests(window, reply);
  free(reply);
}

static void
free_unhanded_replies(HandedReplies* handed)
{
  while (handed->next < handed->count) {
    free(handed->replies[handed->next]);
    handed->replies[handed->next] = NULL;
    handed->next++;
  }
}

static void
fullscreen_reply_test_setup(void)
{
  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  fullscreen_component_init();
  fullscreen_state_read_queue_reset_for_tests();
}

static void
fullscreen_reply_test_shutdown(xcb_ewmh_connection_t* saved_ewmh)
{
  ewmh = saved_ewmh;
  fullscreen_state_read_queue_reset_for_tests();
  client_list_shutdown();
  fullscreen_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * A read queued for state A must not be applied after a keybinding moved the
 * machine to B: the reply carries the property from before that write. A reply
 * queued after the write is applied, so this cannot pass by dropping every reply.
 */
void
test_fullscreen_stale_reply_does_not_overwrite_local_write(void)
{
  LOG_CLEAN("== Testing a stale _NET_WM_STATE reply does not overwrite a local write");

  xcb_ewmh_connection_t* saved = ewmh;
  ewmh                         = NULL;
  fullscreen_reply_test_setup();

  Client* c = client_create(703);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);

  /* The reply says windowed: the property as it was before the keybinding. */
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(1), c->window));

  /* The request the keybinding sends. It transitions, it does not raw-write. */
  hub_send_request(REQ_CLIENT_FULLSCREEN, c->target.id);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  xcb_ewmh_connection_t fake_ewmh;
  memset(&fake_ewmh, 0, sizeof fake_ewmh);
  fake_ewmh._NET_WM_STATE_FULLSCREEN = TEST_FULLSCREEN_ATOM;
  ewmh                               = &fake_ewmh;

  HandedReplies handed = {
    .replies = { wm_state_reply(false), NULL },
    .count   = 1,
  };
  size_t applied = fullscreen_state_read_drain(hand_state_reply, syncing_state_apply, &handed);

  assert(applied == 0);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);
  assert(fullscreen_state_read_queue_count_for_tests() == 0);

  /* Same reply, queued after the write: the current generation is applied. */
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(2), c->window));
  handed.replies[0] = wm_state_reply(false);
  handed.count      = 1;
  handed.next       = 0;
  applied           = fullscreen_state_read_drain(hand_state_reply, syncing_state_apply, &handed);

  assert(applied == 1);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);

  free_unhanded_replies(&handed);
  fullscreen_reply_test_shutdown(saved);
}

/*
 * Applying a reply must not bump the generation. Two reads queued together are
 * both applied, in order. A raw write through fullscreen_set_state is a local
 * write, so a read queued before it is not.
 */
void
test_fullscreen_reply_apply_does_not_bump_generation(void)
{
  LOG_CLEAN("== Testing a _NET_WM_STATE reply does not bump the generation");

  xcb_ewmh_connection_t* saved = ewmh;
  xcb_ewmh_connection_t  fake_ewmh;
  memset(&fake_ewmh, 0, sizeof fake_ewmh);
  fake_ewmh._NET_WM_STATE_FULLSCREEN = TEST_FULLSCREEN_ATOM;
  ewmh                               = &fake_ewmh;

  fullscreen_reply_test_setup();

  Client* c = client_create(704);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);

  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(1), c->window));
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(2), c->window));

  HandedReplies handed = {
    .replies = { wm_state_reply(true), wm_state_reply(false) },
    .count   = 2,
  };
  size_t applied = fullscreen_state_read_drain(hand_state_reply, syncing_state_apply, &handed);

  assert(applied == 2);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);

  /* Queued before the raw write, so the windowed reply must not undo it. */
  assert_or_abort(fullscreen_state_read_queue_push_for_tests(fake_cookie(3), c->window));
  fullscreen_set_state(c, FULLSCREEN_STATE_FULLSCREEN);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  handed.replies[0] = wm_state_reply(false);
  handed.count      = 1;
  handed.next       = 0;
  applied           = fullscreen_state_read_drain(hand_state_reply, syncing_state_apply, &handed);

  assert(applied == 0);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  free_unhanded_replies(&handed);
  fullscreen_reply_test_shutdown(saved);
}

/*
 * A fake EWMH connection for the client-message handler. Nothing is sent: the
 * handler only compares a message's type and atoms with these values.
 */
#define TEST_WM_STATE_ATOM        5151u
#define TEST_UNRELATED_STATE_ATOM 5152u

static void
wm_state_message(xcb_client_message_event_t* message,
                 xcb_window_t                window,
                 uint32_t                    action,
                 xcb_atom_t                  first,
                 xcb_atom_t                  second)
{
  memset(message, 0, sizeof *message);
  message->type           = TEST_WM_STATE_ATOM;
  message->format         = 32;
  message->window         = window;
  message->data.data32[0] = action;
  message->data.data32[1] = first;
  message->data.data32[2] = second;
}

/*
 * A client's own _NET_WM_STATE request is the only way it can make itself
 * fullscreen: EWMH gives the property to the WM. The request asks for a state,
 * and the request it sends to the fullscreen machine is a toggle, so the
 * comparison before the request is what keeps a repeated ADD from unfullscreening
 * the window.
 */
void
test_fullscreen_client_message_request(void)
{
  LOG_CLEAN("== Testing a client's _NET_WM_STATE request moves the fullscreen machine");

  xcb_ewmh_connection_t* saved = ewmh;
  xcb_ewmh_connection_t  fake_ewmh;
  memset(&fake_ewmh, 0, sizeof fake_ewmh);
  fake_ewmh._NET_WM_STATE            = TEST_WM_STATE_ATOM;
  fake_ewmh._NET_WM_STATE_FULLSCREEN = TEST_FULLSCREEN_ATOM;
  ewmh                               = &fake_ewmh;

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  fullscreen_component_init();

  /* The template's actions write the property, and this fake connection is not
     one to send on. The counters answer the same question: did the request
     reach the row the template declares? */
  fake_enter_calls = 0;
  fake_exit_calls  = 0;
  sm_unregister_action("fs_action_enter_fullscreen");
  sm_register_action("fs_action_enter_fullscreen", fake_enter_fullscreen);
  sm_unregister_action("fs_action_exit_fullscreen");
  sm_register_action("fs_action_exit_fullscreen", fake_exit_fullscreen);

  Client* c = client_create(705);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);

  xcb_client_message_event_t message;

  /* ADD, with the fullscreen atom in the first of the two slots. */
  wm_state_message(&message, c->window, 1, TEST_FULLSCREEN_ATOM, 0);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);
  assert(fake_enter_calls == 1);

  /* The same ADD again: the window is already fullscreen, so nothing moves. */
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);
  assert(fake_enter_calls == 1);
  assert(fake_exit_calls == 0);

  /* REMOVE twice: only the first is a change. */
  wm_state_message(&message, c->window, 0, TEST_FULLSCREEN_ATOM, 0);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);
  assert(fake_exit_calls == 1);

  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);
  assert(fake_exit_calls == 1);

  /* TOGGLE follows the machine, whichever state it is in. */
  wm_state_message(&message, c->window, 2, TEST_FULLSCREEN_ATOM, 0);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_WINDOWED);

  /* _NET_WM_STATE names two atoms; the fullscreen one can be in the second. */
  wm_state_message(&message, c->window, 1, TEST_UNRELATED_STATE_ATOM,
                   TEST_FULLSCREEN_ATOM);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  /* Everything a request is not: another atom, another format, another type,
     an unmanaged window, an action EWMH does not number, and no message at all.
     None of them moves the machine. */
  wm_state_message(&message, c->window, 0, TEST_UNRELATED_STATE_ATOM, 0);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  wm_state_message(&message, c->window, 0, TEST_FULLSCREEN_ATOM, 0);
  message.format = 8;
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  wm_state_message(&message, c->window, 0, TEST_FULLSCREEN_ATOM, 0);
  message.type = TEST_UNRELATED_STATE_ATOM;
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  wm_state_message(&message, 9999, 0, TEST_FULLSCREEN_ATOM, 0);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  wm_state_message(&message, c->window, 3, TEST_FULLSCREEN_ATOM, 0);
  fullscreen_on_client_message(&message);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  fullscreen_on_client_message(NULL);
  assert(fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN);

  sm_unregister_action("fs_action_enter_fullscreen");
  sm_register_action("fs_action_enter_fullscreen", fullscreen_action_enter_fullscreen);
  sm_unregister_action("fs_action_exit_fullscreen");
  sm_register_action("fs_action_exit_fullscreen", fullscreen_action_exit_fullscreen);

  ewmh = saved;
  client_list_shutdown();
  fullscreen_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

TEST_GROUP(FullscreenComponent, {
  test_fullscreen_action_registered();
  test_fullscreen_toggle_action();
  test_fullscreen_init_requires_action_registry();
  test_fullscreen_request_refused_for_unmanaged_client();
  test_fullscreen_request_runs_the_template_action();
  test_fullscreen_client_message_request();
  test_fullscreen_state_read_applies_oldest_first();
  test_fullscreen_state_read_stops_at_unarrived_head();
  test_fullscreen_stale_reply_does_not_overwrite_local_write();
  test_fullscreen_reply_apply_does_not_bump_generation();
});
