/*
 * Test Client Target
 *
 * Tests for the Client target entity including:
 * - Client creation and destruction
 * - Sentinel-based client list management
 * - Client property accessors
 * - Hub registration
 */

#include <string.h>
#include "src/sm/sm-registry.h"
#include "src/sm/sm.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"
#include "wm-log.h"

/* Callback helper for foreach tests */
static int g_callback_count = 0;

static void
inc_count_callback(Client* c)
{
  (void) c;
  g_callback_count++;
}

/*
 * Test client list initialization
 */
void
test_client_list_init(void)
{
  LOG_CLEAN("== Testing client list init");

  hub_init();
  sm_registry_init();
  client_list_init();

  /* Verify empty list */
  assert(client_list_is_empty() == true);
  assert(client_list_get_head() == NULL);
  assert(client_list_count() == 0);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client creation
 */
void
test_client_create(void)
{
  LOG_CLEAN("== Testing client creation");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  if (c == NULL) {
    LOG_ERROR("client_create failed");
    abort();
  }
  assert(c != NULL);
  assert(c->window == 100);
  assert(c->target.type == TARGET_TYPE_CLIENT);
  assert(c->target.id == 100);
  assert(c->target.registered == true);

  /* Verify registered with Hub */
  HubTarget* t = hub_get_target_by_id(100);
  if (t == NULL) {
    LOG_ERROR("hub_get_target_by_id failed");
    abort();
  }
  assert(t != NULL);
  assert(t->type == TARGET_TYPE_CLIENT);

  /* Verify in list */
  assert(client_list_get_head() == c);
  assert(client_list_is_empty() == false);
  assert(client_list_count() == 1);

  client_destroy(c);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client creation with XCB_NONE fails
 */
void
test_client_create_none_window(void)
{
  LOG_CLEAN("== Testing client creation with XCB_NONE fails");

  hub_init();
  client_list_init();

  Client* c = client_create(XCB_NONE);
  assert(c == NULL);

  hub_shutdown();
}

/*
 * Test client destruction
 */
void
test_client_destroy(void)
{
  LOG_CLEAN("== Testing client destruction");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  assert(c != NULL);

  /* Verify registered */
  assert(hub_get_target_by_id(100) != NULL);

  client_destroy(c);

  /* Verify unregistered */
  assert(hub_get_target_by_id(100) == NULL);

  /* Verify list is empty */
  assert(client_list_is_empty() == true);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client destroy by window
 */
void
test_client_destroy_by_window(void)
{
  LOG_CLEAN("== Testing client_destroy_by_window");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  assert(c != NULL);
  assert(hub_get_target_by_id(100) != NULL);

  client_destroy_by_window(100);
  assert(hub_get_target_by_id(100) == NULL);

  /* Destroying non-existent should be safe */
  client_destroy_by_window(999);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test sentinel-based client list
 */
void
test_client_sentinel_list(void)
{
  LOG_CLEAN("== Testing sentinel-based client list");

  hub_init();
  sm_registry_init();
  client_list_init();

  /* Create multiple clients */
  Client* c1 = client_create(100);
  Client* c2 = client_create(101);
  Client* c3 = client_create(102);

  assert(c1 != NULL);
  assert(c2 != NULL);
  assert(c3 != NULL);

  /* Verify list order (FIFO - insert at head) */
  assert(client_list_get_head() == c3); /* Last inserted is at head */
  /* Note: tail is sentinel.prev, accessible via iteration if needed */

  /* Verify list count */
  assert(client_list_count() == 3);

  /* Verify contains_window */
  assert(client_list_contains_window(100) == true);
  assert(client_list_contains_window(101) == true);
  assert(client_list_contains_window(102) == true);
  assert(client_list_contains_window(999) == false);

  /* Test next/prev traversal */
  assert(client_get_next(c1) == NULL); /* c1 is tail */
  assert(client_get_next(c2) == c1);
  assert(client_get_next(c3) == c2);

  assert(client_get_prev(c1) == c2);
  assert(client_get_prev(c2) == c3);
  assert(client_get_prev(c3) == NULL); /* c3 is head */

  /* Destroy middle client */
  client_destroy(c2);

  /* Verify list is still linked */
  assert(client_list_count() == 2);
  assert(client_get_next(c3) == c1);
  assert(client_get_prev(c1) == c3);

  /* Clean up */
  client_destroy(c1);
  client_destroy(c3);

  assert(client_list_is_empty() == true);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client foreach iteration
 */
void
test_client_foreach(void)
{
  LOG_CLEAN("== Testing client foreach iteration");

  hub_init();
  sm_registry_init();
  client_list_init();

  /* Create clients */
  client_create(100);
  client_create(101);
  client_create(102);

  /* Count via foreach */
  g_callback_count = 0;
  client_foreach(inc_count_callback);
  assert(g_callback_count == 3);

  /* Count via foreach_reverse */
  g_callback_count = 0;
  client_foreach_reverse(inc_count_callback);
  assert(g_callback_count == 3);

  /* Clean up */
  Client* c = client_list_get_head();
  while (c != NULL) {
    Client* next = client_get_next(c);
    client_destroy(c);
    c = next;
  }

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client property setters/getters
 */
void
test_client_properties(void)
{
  LOG_CLEAN("== Testing client property accessors");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  assert_or_abort(c != NULL);

  /* Test title */
  char* title = strdup("Test Window");
  assert_or_abort(c != NULL);
  client_set_title(c, title);
  assert_or_abort(c->title != NULL);
  assert(strcmp(c->title, "Test Window") == 0);

  /* Test class */
  char* class_name = strdup("test-class");
  assert_or_abort(c != NULL);
  client_set_class(c, class_name);
  assert_or_abort(c->class_name != NULL);
  // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
  assert(strcmp(c->class_name, "test-class") == 0);

  /* Test geometry */
  client_set_geometry(c, 10, 20, 800, 600);
  assert(c->x == 10);
  assert(c->y == 20);
  assert(c->width == 800);
  assert(c->height == 600);

  /* Test border width */
  client_set_border_width(c, 2);
  assert(c->border_width == 2);

  /* Test tags */
  client_set_tags(c, 0xFF);
  assert(c->tags == 0xFF);

  client_add_tag(c, 5);
  assert((c->tags & (1 << 5)) != 0);

  client_remove_tag(c, 5);
  assert((c->tags & (1 << 5)) == 0);

  assert(client_has_tag(c, 0) == true);
  assert(client_has_tag(c, 7) == true);
  assert(client_has_tag(c, 8) == false);

  /* Test state flags */
  client_set_urgent(c, true);
  assert(client_is_urgent(c) == true);

  client_set_managed(c, true);
  assert(client_is_managed(c) == true);

  client_set_focusable(c, false);
  assert(c->focusable == false);
  assert(client_is_focusable(c) == false);

  /* Test mapped state */
  client_set_mapped(c, true);
  assert(client_is_mapped(c) == true);
  client_set_mapped(c, false);
  assert(client_is_mapped(c) == false);

  /* Test stack mode */
  client_set_stack_mode(c, XCB_STACK_MODE_BELOW);
  assert(client_get_stack_mode(c) == XCB_STACK_MODE_BELOW);
  client_set_stack_mode(c, XCB_STACK_MODE_ABOVE);
  assert(client_get_stack_mode(c) == XCB_STACK_MODE_ABOVE);

  client_destroy(c);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client lookup by window
 */
void
test_client_get_by_window(void)
{
  LOG_CLEAN("== Testing client lookup by window");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c1 = client_create(100);
  Client* c2 = client_create(101);

  /* Find existing via Hub */
  Client* found = client_get_by_window(100);
  assert(found == c1);

  found = client_get_by_window(101);
  assert(found == c2);

  /* Find non-existent */
  found = client_get_by_window(999);
  assert(found == NULL);

  /* Clean up */
  client_destroy(c1);
  client_destroy(c2);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client count managed
 */
void
test_client_count_managed(void)
{
  LOG_CLEAN("== Testing client count managed");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c1 = client_create(100);
  Client* c2 = client_create(101);
  Client* c3 = client_create(102);

  assert(client_count_managed() == 0);

  client_set_managed(c1, true);
  assert(client_count_managed() == 1);

  client_set_managed(c2, true);
  assert(client_count_managed() == 2);

  client_set_managed(c3, true);
  assert(client_count_managed() == 3);

  client_set_managed(c2, false);
  assert(client_count_managed() == 2);

  /* Clean up */
  client_destroy(c1);
  client_destroy(c2);
  client_destroy(c3);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test duplicate client creation rejected
 */
void
test_client_duplicate_creation(void)
{
  LOG_CLEAN("== Testing duplicate client creation is rejected");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c1 = client_create(100);
  assert(c1 != NULL);

  /* Try to create another client with same window - should fail */
  Client* c2 = client_create(100);
  assert(c2 == NULL); /* Hub should reject duplicate ID */

  /* Original client should still be valid */
  Client* found = client_get_by_window(100);
  assert(found == c1);

  client_destroy(c1);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client with monitor association
 */
void
test_client_monitor_association(void)
{
  LOG_CLEAN("== Testing client monitor association");

  hub_init();
  sm_registry_init();
  client_list_init();

  Monitor* m = monitor_create(200);
  Client*  c = client_create(100);

  /* Initially no monitor */
  assert(client_get_monitor(c) == NULL);

  /* Set monitor */
  client_set_monitor(c, m);
  assert(client_get_monitor(c) == m);

  /* Client inherits monitor's tags */
  m->tagset = 1 << 2; /* tag 3 */
  client_set_tags(c, m->tagset);
  assert(client_has_tag(c, 2));

  client_destroy(c);
  monitor_destroy(m);

  hub_shutdown();
  sm_registry_shutdown();
}

TEST_GROUP(ClientTarget, {
  test_client_list_init();
  test_client_create();
  test_client_create_none_window();
  test_client_destroy();
  test_client_destroy_by_window();
  test_client_sentinel_list();
  test_client_foreach();
  test_client_properties();
  test_client_get_by_window();
  test_client_count_managed();
  test_client_duplicate_creation();
  test_client_monitor_association();
});
