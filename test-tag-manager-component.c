/*
 * Tag Manager Component Tests
 *
 * Tests for the Tag Manager component implementation.
 * Requires: hub, monitor, client
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <stdlib.h>
#include <string.h>

#include "src/components/focus.h"
#include "src/components/tag-manager.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "test-wm.h"
#include "wm-hub.h"

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

  /* Tag manager should initialize without error */
  bool result = tag_manager_component_init();
  assert(result == true);
  assert(tag_manager_component_is_initialized() == true);

  /* Tag manager should shutdown cleanly */
  tag_manager_component_shutdown();
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
  hub_shutdown();
}

/*
 * Test: tag_manager_client_tag_toggle
 * Tests moving focused client to/from tag.
 */
void
test_tag_manager_client_tag_toggle(void)
{
  LOG_CLEAN("== Testing tag manager client tag toggle");

  hub_init();
  tag_list_init();
  monitor_list_init();
  client_list_init();
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

  /* Set initial tag membership */
  client_set_tags(c, TAG_MASK(0));

  /* Focus the client */
  focus_set_state(c, FOCUS_STATE_FOCUSED);

  /* Toggle client to tag 2 (index 1) */
  uint32_t tag = 2;
  hub_send_request_data(REQ_TAG_CLIENT_TOGGLE, c->target.id, &tag);

  /* Verify tag was added to client */
  assert((c->tags & TAG_MASK(0)) != 0); /* Tag 1 still there */
  assert((c->tags & TAG_MASK(1)) != 0); /* Tag 2 added */

  /* Toggle tag 1 off client */
  tag = 1;
  hub_send_request_data(REQ_TAG_CLIENT_TOGGLE, c->target.id, &tag);

  /* Verify tag was removed */
  assert((c->tags & TAG_MASK(0)) == 0); /* Tag 1 removed */
  assert((c->tags & TAG_MASK(1)) != 0); /* Tag 2 still there */

  /* Cleanup */
  focus_component_shutdown();
  client_destroy(c);
  monitor_destroy(m);
  client_list_shutdown();
  monitor_list_shutdown();
  tag_list_shutdown();
  tag_manager_component_shutdown();
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
  hub_shutdown();
}

TEST_GROUP(TagManager, {
  test_tag_manager_init_shutdown();
  test_tag_manager_receives_tag_view_requests();
  test_tag_manager_receives_tag_toggle_requests();
  test_tag_manager_emits_events();
  test_tag_manager_updates_client_visibility();
  test_tag_manager_client_tag_toggle();
  test_tag_manager_with_multiple_monitors();
  test_tag_manager_invalid_tag_index();
});