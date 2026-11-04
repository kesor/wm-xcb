/*
 * Fullscreen Component Tests
 *
 * Covers the action the default config binds to MOD+f: that the name is
 * registered with a client target, and that invoking it moves the focused
 * client's fullscreen state machine.
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/sm/sm-registry.h"
#include "src/xcb/xcb-handler.h"
#include "test-wm.h"
#include "wm-hub.h"

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
  assert(c->managed == false);

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
  fake_action_owner = (Client*) sm->owner;
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

TEST_GROUP(FullscreenComponent, {
  test_fullscreen_action_registered();
  test_fullscreen_toggle_action();
  test_fullscreen_init_requires_action_registry();
  test_fullscreen_request_refused_for_unmanaged_client();
  test_fullscreen_request_runs_the_template_action();
  test_fullscreen_state_read_applies_oldest_first();
  test_fullscreen_state_read_stops_at_unarrived_head();
});
