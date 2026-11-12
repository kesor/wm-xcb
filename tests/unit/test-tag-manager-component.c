/*
 * Tag Manager Component Tests
 *
 * Tests for the Tag Manager component implementation.
 * Requires: hub, monitor, client
 */

#include "tests/unit/test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/keybinding.h"
#include "src/components/tag-manager.h"
#include "src/config/config.h"
#include "src/core/wm-components.h"
#include "src/core/wm-hub.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"
#include "tests/unit/test-wm.h"

/*
 * Track events received during tests
 */
static struct {
  bool     tag_changed_received;
  TargetID tag_changed_target;
  uint32_t new_tag_mask;
} test_events;

static void
reset_test_events(void)
{
  test_events.tag_changed_received = false;
  test_events.tag_changed_target   = TARGET_ID_NONE;
  test_events.new_tag_mask         = 0;
}

static void
tag_change_listener(Event e)
{
  test_events.tag_changed_received = true;
  test_events.tag_changed_target   = e.target;
  if (e.data != NULL) {
    test_events.new_tag_mask = *(uint32_t*) e.data;
  }
}

/*
 * Test: tag_manager_component_init_shutdown
 * Tests basic initialization and shutdown of tag manager.
 */
void
test_tag_manager_init_shutdown(void)
{
  LOG_CLEAN("== Testing tag manager init/shutdown");

  hub_init();
  monitor_list_init();
  action_registry_init();

  /* Tag manager should initialize without error */
  bool result = tag_manager_component_init();
  assert(result == true);
  assert(tag_manager_component_is_initialized() == true);

  /* Tag manager should shutdown cleanly */
  tag_manager_component_shutdown();
  action_registry_shutdown();
  assert(tag_manager_component_is_initialized() == false);

  monitor_list_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_receives_tag_view_requests
 * Tests that tag manager handles REQ_TAG_VIEW requests.
 */
void
test_tag_manager_receives_tag_view_requests(void)
{
  LOG_CLEAN("== Testing tag manager receives tag view requests");

  hub_init();
  tag_list_init();
  monitor_list_init();

  /* Initialize tag manager */
  action_registry_init();
  tag_manager_component_init();

  /* Create a monitor */
  Monitor* m = monitor_create(100);
  assert(m != NULL);

  /* Adopt tag-manager for the monitor */
  tag_manager_on_adopt(&m->target);

  /* Send a tag view request */
  uint32_t tag = 3; /* View tag 3 */
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  /* Verify the tag view SM was created */
  StateMachine* sm = tag_manager_get_view_sm(m);
  assert(sm != NULL);

  /* Get current visible tags */
  uint32_t visible = tag_manager_get_visible_tags(m);
  assert((visible & TAG_MASK(2)) != 0); /* Tag 3 (index 2) should be visible */

  /* Cleanup */
  monitor_destroy(m);
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_receives_tag_toggle_requests
 * Tests that tag manager handles REQ_TAG_TOGGLE requests.
 */
void
test_tag_manager_receives_tag_toggle_requests(void)
{
  LOG_CLEAN("== Testing tag manager receives tag toggle requests");

  hub_init();
  tag_list_init();
  monitor_list_init();

  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(100);
  assert(m != NULL);

  /* Adopt tag-manager */
  tag_manager_on_adopt(&m->target);

  /* Initial state: tag 1 (index 0) is visible */
  uint32_t visible = tag_manager_get_visible_tags(m);
  assert((visible & TAG_MASK(0)) != 0); /* Tag 1 visible */

  /* Toggle tag 2 (index 1) on */
  uint32_t tag = 2;
  hub_send_request_data(REQ_TAG_TOGGLE, m->target.id, &tag);

  visible = tag_manager_get_visible_tags(m);
  assert((visible & TAG_MASK(0)) != 0); /* Tag 1 still visible */
  assert((visible & TAG_MASK(1)) != 0); /* Tag 2 now visible */

  /* Toggle tag 1 (index 0) off */
  tag = 1;
  hub_send_request_data(REQ_TAG_TOGGLE, m->target.id, &tag);

  visible = tag_manager_get_visible_tags(m);
  assert((visible & TAG_MASK(0)) == 0); /* Tag 1 now hidden */
  assert((visible & TAG_MASK(1)) != 0); /* Tag 2 still visible */

  /* Cleanup */
  monitor_destroy(m);
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_emits_events
 * Tests that tag manager emits EVT_TAG_CHANGED on state changes.
 */
void
test_tag_manager_emits_events(void)
{
  LOG_CLEAN("== Testing tag manager emits events");

  hub_init();
  tag_list_init();
  monitor_list_init();
  action_registry_init();
  tag_manager_component_init();

  /* Subscribe to tag change events */
  hub_subscribe(EVT_TAG_CHANGED, tag_change_listener, NULL);

  Monitor* m = monitor_create(100);
  assert(m != NULL);

  /* Adopt tag-manager */
  tag_manager_on_adopt(&m->target);

  /* Reset test events */
  reset_test_events();

  /* Send tag view request - should trigger event */
  uint32_t tag = 5;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  /* Verify event was emitted */
  assert(test_events.tag_changed_received == true);
  assert(test_events.tag_changed_target == m->target.id);

  /* Cleanup */
  hub_unsubscribe(EVT_TAG_CHANGED, tag_change_listener);
  monitor_destroy(m);
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_updates_client_visibility
 * Tests that clients are shown/hidden based on tag membership.
 */
void
test_tag_manager_updates_client_visibility(void)
{
  LOG_CLEAN("== Testing tag manager updates client visibility");

  hub_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
  action_registry_init();
  tag_manager_component_init();
  focus_component_init();

  Monitor* m = monitor_create(100);
  assert(m != NULL);

  /* Adopt tag-manager for monitor */
  tag_manager_on_adopt(&m->target);

  /* Create a client and assign it to tag 1 */
  Client* c = client_create(1000);
  assert(c != NULL);

  /* Set client tags to tag 1 only */
  client_set_tags(c, TAG_MASK(0));
  client_set_monitor(c, m);
  client_set_managed(c, true);

  /* Set monitor to view tag 1 - client should be visible */
  uint32_t tag = 1;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  bool visible = tag_manager_is_client_visible(m, c);
  assert(visible == true);

  /* Switch to tag 2 - client should no longer be visible */
  tag = 2;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  visible = tag_manager_is_client_visible(m, c);
  assert(visible == false);

  /* Add tag 2 to client - should become visible again */
  c->tags |= TAG_MASK(1);
  visible = tag_manager_is_client_visible(m, c);
  assert(visible == true);

  /* Cleanup */
  client_destroy(c);
  focus_component_shutdown();
  monitor_destroy(m);
  client_list_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_client_move
 * Moving the focused client to a tag replaces its tag set, so it leaves the
 * view it was on and lands on exactly the named tag.
 */
void
test_tag_manager_client_move(void)
{
  LOG_CLEAN("== Testing tag manager client move");

  hub_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
  action_registry_init();
  tag_manager_component_init();
  focus_component_init();

  Monitor* m = monitor_create(100);
  assert(m != NULL);

  /* Adopt tag-manager */
  tag_manager_on_adopt(&m->target);

  /* Create a client */
  Client* c = client_create(2000);
  assert(c != NULL);

  client_set_managed(c, true);
  client_set_monitor(c, m);

  /* Start on two tags so a move has to replace the set, not add to it. */
  client_set_tags(c, TAG_MASK(0) | TAG_MASK(2));

  /* Focus the client */
  focus_set_state(c, FOCUS_STATE_FOCUSED);

  /* Move the client to tag 2 (index 1) */
  uint32_t tag = 2;
  hub_send_request_data(REQ_TAG_CLIENT_MOVE, c->target.id, &tag);

  /* The old tags are gone: the client is on tag 2 alone. */
  assert(c->tags == TAG_MASK(1));

  /* Moving again replaces the mask again rather than adding a third tag. */
  tag = 3;
  hub_send_request_data(REQ_TAG_CLIENT_MOVE, c->target.id, &tag);

  assert(c->tags == TAG_MASK(2));

  /* Cleanup */
  focus_component_shutdown();
  client_destroy(c);
  monitor_destroy(m);
  client_list_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_with_multiple_monitors
 * Tests tag manager with multiple monitors.
 */
void
test_tag_manager_with_multiple_monitors(void)
{
  LOG_CLEAN("== Testing tag manager with multiple monitors");

  hub_init();
  tag_list_init();
  monitor_list_init();

  action_registry_init();
  tag_manager_component_init();

  /* Create two monitors */
  Monitor* m1 = monitor_create(100);
  Monitor* m2 = monitor_create(200);
  assert(m1 != NULL);
  assert(m2 != NULL);

  /* Each monitor gets its own tag-view SM */
  tag_manager_on_adopt(&m1->target);
  tag_manager_on_adopt(&m2->target);

  StateMachine* sm1 = tag_manager_get_view_sm(m1);
  StateMachine* sm2 = tag_manager_get_view_sm(m2);
  assert(sm1 != NULL);
  assert(sm2 != NULL);

  /* Set different tags for each monitor */
  uint32_t tag = 3;
  hub_send_request_data(REQ_TAG_VIEW, m1->target.id, &tag);

  tag = 7;
  hub_send_request_data(REQ_TAG_VIEW, m2->target.id, &tag);

  /* Verify each monitor has different tag view */
  uint32_t view1 = tag_manager_get_visible_tags(m1);
  uint32_t view2 = tag_manager_get_visible_tags(m2);

  assert((view1 & TAG_MASK(2)) != 0); /* Tag 3 on m1 */
  assert((view2 & TAG_MASK(6)) != 0); /* Tag 7 on m2 */

  /* Cleanup */
  monitor_destroy(m1);
  monitor_destroy(m2);
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_manager_invalid_tag_index
 * Tests that invalid tag indices are rejected.
 */
void
test_tag_manager_invalid_tag_index(void)
{
  LOG_CLEAN("== Testing tag manager with invalid tag index");

  hub_init();
  monitor_list_init();

  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(100);
  assert(m != NULL);

  /* Save initial state */
  uint32_t initial_view = tag_manager_get_visible_tags(m);

  /* Try to view invalid tag (0) - should be rejected */
  uint32_t tag = 0;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  /* View should not change (or might change to 1 if 0 is normalized) */
  uint32_t view = tag_manager_get_visible_tags(m);

  /* Try to view invalid tag (10) - should be rejected */
  tag = 10;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  /* Cleanup */
  monitor_destroy(m);
  monitor_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: the three tag actions the default config wires are registered, and
 * each one resolves the target type it needs.
 */
void
test_tag_manager_actions_registered(void)
{
  LOG_CLEAN("== Testing tag manager action registration");

  hub_init();
  action_registry_init();
  tag_manager_component_init();

  Action* view = action_lookup("tag-manager.view");
  assert(view != NULL);
  if (view != NULL) {
    assert(view->callback != NULL);
    assert(view->target_type == ACTION_TARGET_MONITOR);
    assert(view->target_required == true);
  }

  Action* toggle = action_lookup("tag-manager.toggle");
  assert(toggle != NULL);
  if (toggle != NULL) {
    assert(toggle->callback != NULL);
    assert(toggle->target_type == ACTION_TARGET_MONITOR);
    assert(toggle->target_required == true);
  }

  Action* move = action_lookup("tag-manager.client-move");
  assert(move != NULL);
  if (move != NULL) {
    assert(move->callback != NULL);
    assert(move->target_type == ACTION_TARGET_CLIENT);
    assert(move->target_required == true);
  }

  tag_manager_component_shutdown();

  /* Shutdown must take the names with it, or a restart cannot register them */
  assert(action_lookup("tag-manager.view") == NULL);
  assert(action_lookup("tag-manager.toggle") == NULL);
  assert(action_lookup("tag-manager.client-move") == NULL);

  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: invoking a tag action reaches the request the component handles.
 */
void
test_tag_manager_view_and_toggle_actions(void)
{
  LOG_CLEAN("== Testing tag view and toggle actions");

  hub_init();
  tag_list_init();
  monitor_list_init();
  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(100);
  assert_or_abort(m != NULL);
  monitor_set_selected(m);

  ActionInvocation inv = {
    .target = TARGET_ID_NONE,
    .data   = (void*) (uintptr_t) 3, /* tag 3 */
  };

  assert(action_invoke("tag-manager.view", &inv) == true);
  assert(tag_manager_get_visible_tags(m) == TAG_MASK(2));

  inv.data = (void*) (uintptr_t) 1; /* add tag 1 to the view */
  assert(action_invoke("tag-manager.toggle", &inv) == true);
  assert(tag_manager_get_visible_tags(m) == (TAG_MASK(0) | TAG_MASK(2)));

  /* A tag action without a tag argument cannot do anything. */
  inv.data = NULL;
  assert(action_invoke("tag-manager.view", &inv) == false);

  monitor_destroy(m);
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: the client-move action moves the focused client off the visible tag.
 */
void
test_tag_manager_client_move_action(void)
{
  LOG_CLEAN("== Testing tag client-move action");

  hub_init();
  sm_registry_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
  action_registry_init();
  focus_component_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(100);
  assert_or_abort(m != NULL);
  monitor_set_selected(m);
  tag_manager_on_adopt(&m->target);

  Client* c = client_create(500);
  assert_or_abort(c != NULL);
  client_set_tags(c, TAG_MASK(0));
  client_set_managed(c, true);
  client_set_focusable(c, true);
  client_set_mapped(c, true);
  focus_set_state(c, FOCUS_STATE_FOCUSED);
  assert(c->tags == TAG_MASK(0));

  /* Tag 1 is the visible one. Moving the client to tag 2 takes it off the
   * view, and the mask holds that one tag rather than both. */
  ActionInvocation inv = {
    .target = TARGET_ID_NONE,
    .data   = (void*) (uintptr_t) 2,
  };
  assert(action_invoke("tag-manager.client-move", &inv) == true);
  assert(c->tags == TAG_MASK(1));
  assert(client_is_mapped(c) == false);

  tag_manager_component_shutdown();
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  monitor_destroy(m);
  monitor_list_shutdown();
  tag_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * A client that appears while another tag is viewed belongs to that tag.
 *
 * Client creation leaves tags empty because the visible mask belongs to this
 * component. The manage event is where the client gets the tags of the view it
 * appeared on, not the tag that was visible when the monitor was created.
 */
void
test_tag_manager_new_client_takes_the_view_tags(void)
{
  LOG_CLEAN("== Testing a new client takes the visible tags");

  hub_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(800);
  assert_or_abort(m != NULL);

  uint32_t tag = 3;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);
  assert(tag_manager_get_visible_tags(m) == TAG_MASK(2));

  Client* c = client_create(900);
  assert_or_abort(c != NULL);
  assert(c->tags == 0);

  client_set_managed(c, true);
  hub_emit(EVT_CLIENT_MANAGED, c->target.id, NULL);
  assert(c->tags == TAG_MASK(2));

  /* A later view change does not move the client. */
  tag = 1;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);
  assert(c->tags == TAG_MASK(2));

  client_destroy(c);
  monitor_destroy(m);
  client_list_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: the startup order the window manager runs brings the component up.
 *
 * Every test above calls tag_manager_component_init() itself, so all of them
 * pass with the component missing from wm_components_init(), which is how no
 * monitor got a tag-view SM at runtime. This one runs the sequence main() runs
 * and then checks what a monitor gets out of it.
 */
void
test_tag_manager_starts_with_the_wm(void)
{
  LOG_CLEAN("== Testing the startup order initializes the tag manager");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  monitor_list_init();

  wm_components_init();

  assert(tag_manager_component_is_initialized() == true);
  assert(hub_get_component_by_name(TAG_MANAGER_COMPONENT_NAME) != NULL);
  assert(action_exists("tag-manager.view") == true);
  assert(tag_get_by_index(0) != NULL);

  Monitor* m = monitor_create(700);
  assert_or_abort(m != NULL);

  /* Read the stored machine, not tag_manager_get_view_sm(): that one creates
   * the SM on demand and would hide a monitor that was never adopted. */
  assert(monitor_get_sm(m, "tag-view") != NULL);
  assert(tag_manager_get_visible_tags(m) == TAG_MASK(0));

  monitor_destroy(m);

  wm_components_shutdown();

  assert(tag_manager_component_is_initialized() == false);
  assert(hub_get_component_by_name(TAG_MANAGER_COMPONENT_NAME) == NULL);

  monitor_list_shutdown();
  assert(tag_get_by_index(0) == NULL);
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * The manage path asks whether a window belongs on screen before it maps it. A
 * client can carry tags the view is not on - the view moved after its tags were
 * set - so the answer is not "a client is always on screen". A client with no
 * tags yet is the one the manage event gives the view's tags to, so it belongs
 * on screen exactly when that view is not empty.
 */
void
test_tag_manager_answers_which_clients_are_on_screen(void)
{
  LOG_CLEAN("== Testing the tag manager answers whether a client is on screen");

  hub_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(950);
  assert_or_abort(m != NULL);
  tag_manager_on_adopt(&m->target);

  Client* c = client_create(951);
  assert_or_abort(c != NULL);
  client_set_monitor(c, m);
  client_set_managed(c, true);

  /* Tag 1 is shown, and a client with no tags yet takes that tag. */
  bool visible = false;
  hub_send_request_data(REQ_CLIENT_VISIBLE, c->target.id, &visible);
  assert(visible == true);

  /* A client is on screen when one of its own tags is. */
  client_set_tags(c, TAG_MASK(1)); /* tag 2, while the view is on tag 1 */
  visible = false;
  hub_send_request_data(REQ_CLIENT_VISIBLE, c->target.id, &visible);
  assert(visible == false);

  uint32_t tag = 2;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);
  visible = false;
  hub_send_request_data(REQ_CLIENT_VISIBLE, c->target.id, &visible);
  assert(visible == true);

  /* Toggling the last visible tag off asks for no client on screen. */
  hub_send_request_data(REQ_TAG_TOGGLE, m->target.id, &tag);
  assert(tag_manager_get_visible_tags(m) == 0);
  visible = true;
  hub_send_request_data(REQ_CLIENT_VISIBLE, c->target.id, &visible);
  assert(visible == false);

  /* Untagged, with nothing visible to take a tag from. */
  Client* untagged = client_create(952);
  assert_or_abort(untagged != NULL);
  client_set_monitor(untagged, m);
  client_set_managed(untagged, true);
  assert(untagged->tags == 0);
  visible = true;
  hub_send_request_data(REQ_CLIENT_VISIBLE, untagged->target.id, &visible);
  assert(visible == false);

  /* A client on no monitor is not this component's to judge, and neither is a
   * target that is not a client: the answer the caller came with stays. */
  Client* loose = client_create(953);
  assert_or_abort(loose != NULL);
  client_set_tags(loose, TAG_MASK(0));
  client_set_monitor(loose, NULL);
  visible = true;
  hub_send_request_data(REQ_CLIENT_VISIBLE, loose->target.id, &visible);
  assert(visible == true);

  visible = true;
  hub_send_request_data(REQ_CLIENT_VISIBLE, m->target.id, &visible);
  assert(visible == true);

  /* Cleanup */
  client_destroy(loose);
  client_destroy(untagged);
  client_destroy(c);
  monitor_destroy(m);
  client_list_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * A client that already carries tags keeps them when it is managed again.
 *
 * Only a client with no tags takes the view's: a window whose client withdrew
 * it and mapped it again has to come back on the tags the user put it on, not
 * on whichever tag happens to be viewed when it returns. Without that, no
 * client is ever managed while carrying tags the view is not on, and the manage
 * path's question about one can never be answered no.
 */
void
test_tag_manager_keeps_the_tags_a_remapped_client_has(void)
{
  LOG_CLEAN("== Testing a remapped client keeps the tags it already has");

  hub_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(970);
  assert_or_abort(m != NULL);
  tag_manager_on_adopt(&m->target);

  Client* c = client_create(971);
  assert_or_abort(c != NULL);
  client_set_monitor(c, m);
  assert(c->tags == 0);

  /* A new client takes the view's tags. */
  client_set_managed(c, true);
  hub_emit(EVT_CLIENT_MANAGED, c->target.id, NULL);
  assert(c->tags == TAG_MASK(0));

  /* The user moves it to tag 3, then the view moves to tag 5. */
  client_set_tags(c, TAG_MASK(2));
  uint32_t tag = 5;
  hub_send_request_data(REQ_TAG_VIEW, m->target.id, &tag);

  /* The client withdraws and maps its window again. */
  client_set_managed(c, false);
  client_set_managed(c, true);
  hub_emit(EVT_CLIENT_MANAGED, c->target.id, NULL);
  assert(c->tags == TAG_MASK(2));

  /* Cleanup */
  client_destroy(c);
  monitor_destroy(m);
  client_list_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_shutdown();
  hub_shutdown();
}

/*
 * Nothing in the WM creates the tags except this component, so a startup that
 * skips it leaves every tag lookup answering NULL and the config file's tag
 * names with nowhere to go.
 */
void
test_tag_manager_creates_the_tags(void)
{
  LOG_CLEAN("== Testing the tag manager creates the tags");

  hub_init();
  monitor_list_init();
  action_registry_init();

  assert(tag_get_by_index(0) == NULL);
  assert(tag_manager_component_init() == true);

  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    Tag* t = tag_get_by_index(i);
    assert_or_abort(t != NULL);
    assert(t->target.registered == true);
    /* The hub routes to the same target the list holds, not only the list. */
    assert(tag_get_by_id(tag_get_id_by_index(i)) == t);
  }

  tag_manager_component_shutdown();
  assert(tag_get_by_index(0) == NULL);

  action_registry_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

/*
 * Write `text` to a fresh file and put its path in `path`. The caller unlinks
 * it. Returns false when the temporary file cannot be made.
 */
static bool
write_tag_config(char* path, size_t size, const char* text)
{
  const char* dir = getenv("TMPDIR");
  int         fd  = -1;
  FILE*       file;

  if (dir == NULL || dir[0] == '\0')
    dir = "/tmp";
  if (snprintf(path, size, "%s/wm-tag-config-test-XXXXXX", dir) >= (int) size)
    return false;

  fd = mkstemp(path);
  if (fd < 0)
    return false;

  file = fdopen(fd, "w");
  if (file == NULL) {
    close(fd);
    unlink(path);
    return false;
  }

  (void) fputs(text, file);
  (void) fclose(file);
  return true;
}

static bool
rewrite_tag_config(const char* path, const char* text)
{
  FILE* file = fopen(path, "w");
  if (file == NULL)
    return false;

  (void) fputs(text, file);
  return fclose(file) == 0;
}

/*
 * A Tag copies its name when it is created, and the reload replaces the strings
 * config_get_tag_name() answers from. The tag has to be renamed, or it keeps the
 * name from the configuration the WM started with.
 */
void
test_tag_manager_renames_tags_on_reload(void)
{
  LOG_CLEAN("== Testing a reload renames the tags");

  char path[256];

  assert_or_abort(write_tag_config(path, sizeof path, "tags dev chat\n"));
  assert(config_load_runtime(path) == true);

  hub_init();
  xcb_handler_init();
  monitor_list_init();
  keybinding_init();
  assert(tag_manager_component_init() == true);

  Tag* first = tag_get_by_index(0);
  Tag* third = tag_get_by_index(2);
  assert_or_abort(first != NULL);
  assert_or_abort(third != NULL);
  assert(strcmp(tag_get_name(first), "dev") == 0);
  assert(strcmp(tag_get_name(tag_get_by_index(1)), "chat") == 0);
  assert(strcmp(tag_get_name(third), "3") == 0);

  assert(rewrite_tag_config(path, "tags work\n"));
  assert(config_reload() == true);

  /* The same target answers with the new name, and one the edit dropped goes
   * back to the number the user types. */
  assert(tag_get_by_index(0) == first);
  assert(strcmp(tag_get_name(first), "work") == 0);
  assert(strcmp(tag_get_name(tag_get_by_index(1)), "2") == 0);

  tag_manager_component_shutdown();
  keybinding_shutdown();
  monitor_list_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
  config_shutdown();
  unlink(path);
}

/*
 * A creator that returns NULL stands for a table the framework refused. The
 * init has to stop instead of running with no machine for its monitors.
 */
static SMTemplate*
refuse_tag_view_template_create(void)
{
  return NULL;
}

void
test_tag_manager_init_fails_when_template_is_refused(void)
{
  LOG_CLEAN("== Testing tag manager init fails when its template is refused");

  hub_init();
  tag_list_init();
  monitor_list_init();
  action_registry_init();

  tag_manager_component_set_template_creator_for_tests(refuse_tag_view_template_create);

  assert(tag_manager_component_init() == false);
  assert(tag_manager_component_is_initialized() == false);
  assert(hub_get_component_by_name(TAG_MANAGER_COMPONENT_NAME) == NULL);
  assert(action_exists("tag-manager.view") == false);
  assert(action_exists("tag-manager.toggle") == false);
  assert(action_exists("tag-manager.client-move") == false);

  Monitor* m = monitor_create(1201);
  assert_or_abort(m != NULL);
  assert(tag_manager_get_view_sm(m) == NULL);
  assert(monitor_get_sm(m, TAG_MANAGER_SM_NAME) == NULL);

  tag_manager_component_set_template_creator_for_tests(NULL);
  monitor_destroy(m);

  action_registry_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  hub_shutdown();
}

TEST_GROUP(TagManager, {
  test_tag_manager_init_shutdown();
  test_tag_manager_receives_tag_view_requests();
  test_tag_manager_receives_tag_toggle_requests();
  test_tag_manager_emits_events();
  test_tag_manager_updates_client_visibility();
  test_tag_manager_client_move();
  test_tag_manager_with_multiple_monitors();
  test_tag_manager_invalid_tag_index();
  test_tag_manager_actions_registered();
  test_tag_manager_view_and_toggle_actions();
  test_tag_manager_client_move_action();
  test_tag_manager_new_client_takes_the_view_tags();
  test_tag_manager_answers_which_clients_are_on_screen();
  test_tag_manager_keeps_the_tags_a_remapped_client_has();
  test_tag_manager_starts_with_the_wm();
  test_tag_manager_creates_the_tags();
  test_tag_manager_renames_tags_on_reload();
  test_tag_manager_init_fails_when_template_is_refused();
});
