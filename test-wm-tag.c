/*
 * Tag Tests
 *
 * Tests for the Tag entity implementation.
 * Requires: hub
 *
 * Note: Each test function manages its own setup/cleanup for test isolation.
 * Tests that need the default 9 tags call tag_list_init() at the start
 * and tag_list_shutdown() at the end. Other tests create specific tags.
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "src/components/pertag.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "test-wm-tag.h"
#include "test-wm.h"
#include "wm-hub.h"

/*
 * Global callback for tag iteration tests
 */
static int tag_iteration_count = 0;
static int tag_iteration_indices[TAG_NUM_TAGS];

static void
tag_iteration_callback(Tag* t)
{
  if (tag_iteration_count < TAG_NUM_TAGS) {
    tag_iteration_indices[tag_iteration_count++] = t->index;
  }
}

/*
 * Test: tag_create_destroy
 * Tests creating and destroying a single tag using a specific index.
 */
void
test_tag_create_destroy(void)
{
  LOG_CLEAN("== Testing tag create and destroy");

  hub_init();

  /* Create a tag using index 0 directly (not via tag_list_init) */
  Tag* t = tag_create(0, "test");
  if (t == NULL) {
    LOG_ERROR("tag_create failed");
    abort();
  }
  assert(t != NULL);
  assert(t->index == 0);
  assert(t->target.id != TARGET_ID_NONE);
  assert(t->target.type_id == hub_get_target_type_id_by_name("tag"));
  assert(t->target.registered == true);
  assert(t->mask == TAG_MASK(0));
  /* name may be NULL when tag_create is called with a name but the allocation fails
   * or when called without a name. For this test, we pass "test" so it should exist. */
  if (t->name != NULL) {
    assert(strcmp(t->name, "test") == 0);
  }

  /* Verify it's registered with hub */
  HubTarget* target = hub_get_target_by_id(t->target.id);
  if (target == NULL) {
    LOG_ERROR("hub_get_target_by_id failed");
    abort();
  }
  assert(target != NULL);
  assert(target->type_id == hub_get_target_type_id_by_name("tag"));

  /* Destroy - save ID first to avoid use-after-free */
  TargetID saved_id = t->target.id;
  tag_destroy(t);
  assert(hub_get_target_by_id(saved_id) == NULL);

  hub_shutdown();
}

/*
 * Test: tag_list_init_shutdown
 * Tests tag list initialization with default tags.
 */
void
test_tag_list_init_shutdown(void)
{
  LOG_CLEAN("== Testing tag list init and shutdown");

  hub_init();
  tag_list_init();

  /* List should have 9 default tags */
  uint32_t count = 0;
  Tag*     t;
  for (t = tag_list_get_first(); t != NULL; t = tag_list_get_next(t)) {
    count++;
  }
  assert(count == TAG_NUM_TAGS);

  /* Shutdown should clean up all tags */
  tag_list_shutdown();
  assert(tag_list_get_first() == NULL);

  hub_shutdown();
}

/*
 * Test: tag_list_iteration
 * Tests iterating over all tags.
 */
void
test_tag_list_iteration(void)
{
  LOG_CLEAN("== Testing tag list iteration");

  hub_init();
  tag_list_init();

  /* Iterate over all tags */
  tag_iteration_count = 0;
  tag_foreach(tag_iteration_callback);

  assert(tag_iteration_count == TAG_NUM_TAGS);

  /* Verify we visited all indices 0-8 */
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    bool found = false;
    for (int j = 0; j < tag_iteration_count; j++) {
      if (tag_iteration_indices[j] == i) {
        found = true;
        break;
      }
    }
    assert(found);
  }

  tag_list_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_get_by_index
 * Tests getting a tag by its index.
 */
void
test_tag_get_by_index(void)
{
  LOG_CLEAN("== Testing tag get by index");

  hub_init();
  tag_list_init();

  /* Get each default tag */
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    Tag* t = tag_get_by_index(i);
    if (t == NULL) {
      LOG_ERROR("tag_get_by_index(%d) returned NULL", i);
      abort();
    }
    if (t->index != i) {
      LOG_ERROR("tag index mismatch: expected %d, got %d", i, t->index);
      abort();
    }
    if (t->mask != TAG_MASK(i)) {
      LOG_ERROR("tag mask mismatch for index %d", i);
      abort();
    }
  }

  /* Invalid indices return NULL */
  assert(tag_get_by_index(-1) == NULL);
  assert(tag_get_by_index(TAG_NUM_TAGS) == NULL);

  tag_list_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_get_by_name
 * Tests getting a tag by its name.
 * Uses indices 0 and 1 since tag_list is empty (no tag_list_init).
 */
void
test_tag_get_by_name(void)
{
  LOG_CLEAN("== Testing tag get by name");

  hub_init();

  /* Create specific tags with names (tag list is empty) */
  Tag* t1 = tag_create(0, "work");
  assert(t1 != NULL);
  Tag* t2 = tag_create(1, "web");
  assert(t2 != NULL);

  /* Find by name */
  Tag* found = tag_get_by_name("work");
  assert(found == t1);

  found = tag_get_by_name("web");
  assert(found == t2);

  /* Non-existent name */
  found = tag_get_by_name("nonexistent");
  assert(found == NULL);

  /* Clean up */
  tag_destroy(t1);
  tag_destroy(t2);

  hub_shutdown();
}

/*
 * Test: tag_target_id_conversion
 * Tests conversion between tag indices and TargetIDs.
 */
void
test_tag_target_id_conversion(void)
{
  LOG_CLEAN("== Testing tag target ID conversion");

  hub_init();
  tag_list_init();

  /* Test conversion for each tag */
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    TargetID id = tag_index_to_target_id(i);
    assert(id != TARGET_ID_NONE);

    int index = tag_target_id_to_index(id);
    assert(index == i);
  }

  /* Test is_tag_target check */
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    TargetID id = tag_index_to_target_id(i);
    assert(tag_is_tag_target(id) == true);
  }

  /* Non-tag ID should return false */
  assert(tag_is_tag_target(123) == false);

  /* Invalid index */
  assert(tag_index_to_target_id(-1) == TARGET_ID_NONE);
  assert(tag_index_to_target_id(TAG_NUM_TAGS) == TARGET_ID_NONE);

  /* Invalid ID */
  assert(tag_target_id_to_index(0x12345678) == -1);

  tag_list_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_with_hub_integration
 * Tests tag integration with the Hub registry.
 */
void
test_tag_with_hub_integration(void)
{
  LOG_CLEAN("== Testing tag hub integration");

  hub_init();
  tag_list_init();

  /* All tags should be registered */
  uint32_t    tag_type_id = hub_get_target_type_id_by_name("tag");
  HubTarget** tags        = hub_get_targets_by_type(tag_type_id);
  assert(tags != NULL);

  /* Count TAG targets */
  uint32_t count = 0;
  while (tags[count] != NULL) {
    count++;
  }
  assert(count == TAG_NUM_TAGS);

  /* Verify TAG targets are separate from MONITOR targets */
  uint32_t    monitor_type_id = hub_get_target_type_id_by_name("monitor");
  HubTarget** monitors        = hub_get_targets_by_type(monitor_type_id);
  if (monitors != NULL) {
    assert(monitors[0] == NULL); /* No monitors created yet */
  }

  /* Verify TAG targets are separate from CLIENT targets */
  uint32_t    client_type_id = hub_get_target_type_id_by_name("client");
  HubTarget** clients        = hub_get_targets_by_type(client_type_id);
  if (clients != NULL) {
    assert(clients[0] == NULL); /* No clients created yet */
  }

  tag_list_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_iterate_by_mask
 * Tests iterating over tags matching a bitmask.
 */
void
test_tag_iterate_by_mask(void)
{
  LOG_CLEAN("== Testing tag iterate by mask");

  hub_init();
  tag_list_init();

  /* Test single tag mask */
  tag_iteration_count = 0;
  tag_iterate_by_mask(TAG_MASK(2), tag_iteration_callback);
  assert(tag_iteration_count == 1);
  assert(tag_iteration_indices[0] == 2);

  /* Test multiple tag mask */
  tag_iteration_count = 0;
  tag_iterate_by_mask(TAG_MASK(0) | TAG_MASK(3) | TAG_MASK(8), tag_iteration_callback);
  assert(tag_iteration_count == 3);

  /* Test all tags mask */
  tag_iteration_count = 0;
  tag_iterate_by_mask(TAG_ALL_TAGS, tag_iteration_callback);
  assert(tag_iteration_count == TAG_NUM_TAGS);

  /* Test empty mask */
  tag_iteration_count = 0;
  tag_iterate_by_mask(0, tag_iteration_callback);
  assert(tag_iteration_count == 0);

  tag_list_shutdown();
  hub_shutdown();
}

/*
 * Test: tag_multiple_monitors_reference
 * Tests that multiple monitors can reference tags.
 *
 * Note: This test explicitly adopts the pertag component for each monitor.
 * In the new design, pertag is a separate component that components
 * need to adopt before use.
 */
void
test_tag_multiple_monitors_reference(void)
{
  LOG_CLEAN("== Testing multiple monitors referencing tags");

  hub_init();
  tag_list_init();
  monitor_list_init();

  /* Create monitors */
  Monitor* m1 = monitor_create(100);
  Monitor* m2 = monitor_create(200);

  assert(m1 != NULL);
  assert(m2 != NULL);

  /* Adopt pertag component for each monitor */
  pertag_on_adopt(&m1->target);
  pertag_on_adopt(&m2->target);

  /* Verify each monitor has pertag data */
  assert(pertag_has_data(m1) == true);
  assert(pertag_has_data(m2) == true);

  /* Verify monitors track separate tag state */
  assert(pertag_get_curtag(m1) == 1);
  assert(pertag_get_curtag(m2) == 1);

  /* Set different tags for each monitor */
  pertag_view(m1, 3);
  pertag_view(m2, 7);

  assert(pertag_get_curtag(m1) == 3);
  assert(pertag_get_curtag(m2) == 7);

  /* Verify each monitor can get tag target IDs */
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    TargetID tag_id = tag_get_id_by_index(i);
    assert(tag_id != TARGET_ID_NONE);
    assert(tag_is_tag_target(tag_id) == true);
  }

  /* Clean up - monitors destroy pertag data on destroy */
  monitor_destroy(m1);
  monitor_destroy(m2);

  monitor_list_shutdown();
  tag_list_shutdown();
  hub_shutdown();
}

TEST_GROUP(Tag, {
  test_tag_create_destroy();
  test_tag_list_init_shutdown();
  test_tag_list_iteration();
  test_tag_get_by_index();
  test_tag_get_by_name();
  test_tag_target_id_conversion();
  test_tag_with_hub_integration();
  test_tag_iterate_by_mask();
  test_tag_multiple_monitors_reference();
});
