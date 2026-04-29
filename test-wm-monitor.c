/*
 * Monitor Tests
 *
 * Tests for the Monitor entity implementation.
 * Requires: hub, window-list
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <assert.h>
#include <stdlib.h>

#include "test-wm.h"
#include "test-wm-monitor.h"
#include "src/target/monitor.h"
#include "src/target/client.h"
#include "wm-hub.h"

void
test_monitor_create_destroy(void)
{
  LOG_CLEAN("== Testing monitor create and destroy");

  hub_init();
  monitor_list_init();

  /* Create a monitor */
  xcb_randr_output_t output = 100;
  Monitor*           m      = monitor_create(output);

  assert(m != NULL);
  assert(m->target.id == (TargetID) output);
  assert(m->target.type == TARGET_TYPE_MONITOR);
  assert(m->target.registered == true);
  assert(m->output == output);
  assert(m->crtc == XCB_NONE);
  assert(m->x == 0);
  assert(m->y == 0);
  assert(m->width == 0);
  assert(m->height == 0);
  assert(m->tagset == MONITOR_TAG_MASK(0));
  assert(m->mfact == 0.5f);
  assert(m->nmaster == 1);
  assert(m->client_count == 0);
  assert(m->sel_window == XCB_NONE);
  assert(m->stack_head == XCB_NONE);
  assert(m->bar == NULL);
  assert(m->next == NULL);

  /* Verify it's registered with hub */
  HubTarget* t = hub_get_target_by_id((TargetID) output);
  assert(t != NULL);
  assert(t->type == TARGET_TYPE_MONITOR);

  /* Destroy */
  monitor_destroy(m);
  assert(hub_get_target_by_id((TargetID) output) == NULL);

  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_list_init_shutdown(void)
{
  LOG_CLEAN("== Testing monitor list init and shutdown");

  hub_init();
  monitor_list_init();

  /* List should be empty */
  assert(monitor_list_get_first() == NULL);

  /* Add a monitor */
  Monitor* m = monitor_create(100);
  assert(monitor_list_get_first() == m);

  /* Shutdown should clean up all monitors */
  monitor_list_shutdown();
  assert(monitor_list_get_first() == NULL);

  hub_shutdown();
}

void
test_monitor_list_add_remove(void)
{
  LOG_CLEAN("== Testing monitor list add and remove");

  hub_init();
  monitor_list_init();

  Monitor* m1 = monitor_create(100);
  Monitor* m2 = monitor_create(200);

  /* m2 should be at head (most recent) */
  assert(monitor_list_get_first() == m2);
  assert(monitor_list_get_next(m2) == m1);
  assert(monitor_list_get_next(m1) == NULL);

  /* Remove m2 */
  monitor_list_remove(m2);
  monitor_destroy(m2); /* Properly destroy after remove */
  assert(monitor_list_get_first() == m1);

  /* Remove m1 */
  monitor_destroy(m1);

  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_list_iteration(void)
{
  LOG_CLEAN("== Testing monitor list iteration");

  hub_init();
  monitor_list_init();

  /* Create three monitors */
  Monitor* m1 = monitor_create(100);
  Monitor* m2 = monitor_create(200);
  Monitor* m3 = monitor_create(300);

  /* Count monitors via iteration */
  uint32_t count = 0;
  Monitor* m;
  for (m = monitor_list_get_first(); m != NULL; m = monitor_list_get_next(m)) {
    count++;
  }
  assert(count == 3);

  /* Clean up */
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_find_by_output(void)
{
  LOG_CLEAN("== Testing monitor find by output");

  hub_init();
  monitor_list_init();

  Monitor* m1 = monitor_create(100);
  Monitor* m2 = monitor_create(200);

  /* Find existing */
  Monitor* found = monitor_by_output(100);
  assert(found == m1);

  found = monitor_by_output(200);
  assert(found == m2);

  /* Find non-existent */
  found = monitor_by_output(999);
  assert(found == NULL);

  /* Clean up */
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_selection(void)
{
  LOG_CLEAN("== Testing monitor selection");

  hub_init();
  monitor_list_init();

  Monitor* m1 = monitor_create(100);
  Monitor* m2 = monitor_create(200);

  /* Last created monitor should be selected by default */
  assert(monitor_get_selected() == m2);

  /* Select m1 */
  monitor_set_selected(m1);
  assert(monitor_get_selected() == m1);

  /* Select m2 */
  monitor_set_selected(m2);
  assert(monitor_get_selected() == m2);

  /* Select NULL */
  monitor_set_selected(NULL);
  assert(monitor_get_selected() == NULL);

  /* Destroy selected monitor - should fall back to first remaining */
  monitor_set_selected(m1);
  monitor_destroy(m1);
  assert(monitor_get_selected() == m2);

  /* Clean up */
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_tag_operations(void)
{
  LOG_CLEAN("== Testing monitor tag operations");

  hub_init();
  monitor_list_init();

  Monitor* m = monitor_create(100);

  /* Default tagset should be tag 0 */
  assert(monitor_tag_visible(m, 0) == true);
  assert(monitor_tag_visible(m, 1) == false);

  /* Set tagset */
  monitor_set_tagset(m, MONITOR_TAG_MASK(1) | MONITOR_TAG_MASK(2));
  assert(monitor_tag_visible(m, 1) == true);
  assert(monitor_tag_visible(m, 2) == true);
  assert(monitor_tag_visible(m, 0) == false);

  /* prevtagset should be saved */
  assert(m->prevtagset == MONITOR_TAG_MASK(0));

  /* Tag add/remove */
  monitor_tag_add(m, 3);
  assert(monitor_tag_visible(m, 3) == true);

  monitor_tag_remove(m, 1);
  assert(monitor_tag_visible(m, 1) == false);

  /* Tag toggle */
  monitor_tag_toggle(m, 2);
  assert(monitor_tag_visible(m, 2) == false);
  monitor_tag_toggle(m, 2);
  assert(monitor_tag_visible(m, 2) == true);

  /* Invalid tag index should be safe */
  assert(monitor_tag_visible(m, -1) == false);
  assert(monitor_tag_visible(m, 9) == false); /* MONITOR_NUM_TAGS = 9 */

  /* Clean up */
  monitor_destroy(m);
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_geometry(void)
{
  LOG_CLEAN("== Testing monitor geometry");

  hub_init();
  monitor_list_init();

  Monitor* m = monitor_create(100);

  /* Default geometry is 0 */
  assert(m->x == 0);
  assert(m->y == 0);
  assert(m->width == 0);
  assert(m->height == 0);

  /* Set geometry */
  m->x      = 0;
  m->y      = 0;
  m->width  = 1920;
  m->height = 1080;

  assert(m->x == 0);
  assert(m->y == 0);
  assert(m->width == 1920);
  assert(m->height == 1080);

  /* Set position */
  m->x = 1920;
  m->y = 0;
  assert(m->x == 1920);
  assert(m->y == 0);

  /* Clean up */
  monitor_destroy(m);
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_tiling_params(void)
{
  LOG_CLEAN("== Testing monitor tiling parameters");

  hub_init();
  monitor_list_init();

  Monitor* m = monitor_create(100);

  /* Default tiling params */
  assert(m->mfact == 0.5f);
  assert(m->nmaster == 1);

  /* Set mfact */
  m->mfact = 0.6f;
  assert(m->mfact == 0.6f);

  m->mfact = 0.3f;
  assert(m->mfact == 0.3f);

  /* Set nmaster */
  m->nmaster = 2;
  assert(m->nmaster == 2);

  m->nmaster = 0;
  assert(m->nmaster == 0);

  /* Clean up */
  monitor_destroy(m);
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_client_list(void)
{
  LOG_CLEAN("== Testing monitor client list");

  hub_init();
  monitor_list_init();
  client_list_init();

  Monitor* m = monitor_create(100);

  /* Monitor should have no clients initially */
  assert(monitor_has_clients(m) == false);
  assert(monitor_client_count(m) == 0);

  /* Clean up */
  client_list_shutdown();
  monitor_destroy(m);
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_multiple_monitors(void)
{
  LOG_CLEAN("== Testing multiple monitors scenario");

  hub_init();
  monitor_list_init();

  /* Create monitors with different outputs */
  Monitor* m1 = monitor_create(100); /* Primary */
  m1->x       = 0;
  m1->y       = 0;
  m1->width   = 1920;
  m1->height  = 1080;

  Monitor* m2 = monitor_create(200); /* Secondary */
  m2->x       = 1920;
  m2->y       = 0;
  m2->width   = 1920;
  m2->height  = 1080;

  Monitor* m3 = monitor_create(300); /* Tertiary */
  m3->x       = 0;
  m3->y       = 1080;
  m3->width   = 3840;
  m3->height  = 2160;

  /* All three should be registered */
  assert(hub_get_target_by_id(100) != NULL);
  assert(hub_get_target_by_id(200) != NULL);
  assert(hub_get_target_by_id(300) != NULL);

  /* Selection */
  assert(monitor_get_selected() == m3); /* Last created */

  /* Destroy middle monitor */
  monitor_destroy(m2);
  assert(hub_get_target_by_id(200) == NULL);
  assert(monitor_by_output(200) == NULL);

  /* m1 and m3 should still exist */
  assert(monitor_by_output(100) == m1);
  assert(monitor_by_output(300) == m3);

  /* Clean up */
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_double_create(void)
{
  LOG_CLEAN("== Testing double monitor create with same output");

  hub_init();
  monitor_list_init();

  /* Create monitor with output 100 */
  Monitor* m1 = monitor_create(100);
  assert(m1 != NULL);

  /* Try to create another with same output - will create a new monitor
   * but hub will reject duplicate ID */
  Monitor* m2 = monitor_create(100); /* This should fail in hub due to duplicate ID */

  /* m2 might be created but not registered, or registration fails */
  /* The current implementation allows creating with duplicate ID but
   * hub registration will fail. Let's verify at least one is registered. */
  assert(hub_get_target_by_id(100) != NULL);

  /* Clean up */
  monitor_destroy(m1);
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_null_destroy(void)
{
  LOG_CLEAN("== Testing NULL monitor destroy is safe");

  hub_init();
  monitor_list_init();

  /* NULL destroy should be safe (no crash) */
  monitor_destroy(NULL);

  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_with_hub_integration(void)
{
  LOG_CLEAN("== Testing monitor hub integration");

  hub_init();
  monitor_list_init();

  /* Register a test component that handles monitors */
  static RequestType monitor_requests[] = { 1, 0 };
  static TargetType  monitor_targets[]  = { TARGET_TYPE_MONITOR, TARGET_TYPE_NONE };

  static HubComponent test_component = {
    .name       = "test-monitor-component",
    .requests   = monitor_requests,
    .targets    = monitor_targets,
    .registered = false,
  };

  hub_register_component(&test_component);
  assert(test_component.registered == true);

  /* Create a monitor - it should be registered with hub */
  Monitor* m = monitor_create(100);
  assert(hub_get_target_by_id(100) != NULL);

  /* Get components for monitor type */
  HubTarget** monitors = hub_get_targets_by_type(TARGET_TYPE_MONITOR);
  assert(monitors != NULL);
  assert(monitors[0] == &m->target);
  assert(monitors[1] == NULL); /* NULL-terminated */

  /* Verify client type is separate */
  HubTarget** clients = hub_get_targets_by_type(TARGET_TYPE_CLIENT);
  assert(clients != NULL);
  assert(clients[0] == NULL); /* No clients */

  /* Clean up */
  hub_unregister_component("test-monitor-component");
  monitor_destroy(m);
  monitor_list_shutdown();
  hub_shutdown();
}

TEST_GROUP(Monitor, {
  test_monitor_create_destroy();
  test_monitor_list_init_shutdown();
  test_monitor_list_add_remove();
  test_monitor_list_iteration();
  test_monitor_find_by_output();
  test_monitor_selection();
  test_monitor_tag_operations();
  test_monitor_geometry();
  test_monitor_tiling_params();
  test_monitor_client_list();
  test_monitor_multiple_monitors();
  test_monitor_double_create();
  test_monitor_null_destroy();
  test_monitor_with_hub_integration();
});
