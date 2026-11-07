/*
 * Monitor Tests
 *
 * Tests for the Monitor entity implementation.
 * Requires: hub
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <assert.h>
#include <stdlib.h>

#include "src/core/wm-hub.h"
#include "src/target/monitor.h"
#include "test-wm-monitor.h"
#include "test-wm.h"

/* Set by the observer below when a monitor is unregistered. */
static StateMachine* g_unadopt_seen_machine;

static const char* g_observer_accepts[] = { "monitor", NULL };

static void
observer_on_unadopt(HubTarget* target)
{
  g_unadopt_seen_machine = monitor_get_sm((Monitor*) target, "probe");
}

static HubComponent g_observer_component = {
  .name                  = "monitor-unadopt-observer",
  .accepted_target_names = g_observer_accepts,
  .on_unadopt            = observer_on_unadopt,
};

/*
 * Test: destroying a monitor unadopts its components before it drops the
 * machines they stored on it.
 *
 * The other order left each unadopt hook writing into a table that had just
 * been cleared, so the hook could not reach the machine it was asked to clean
 * up. The tag view machine's data is freed through the machine, and it leaked
 * on every monitor the tests destroyed.
 */
void
test_monitor_destroy_unadopts_before_freeing_machines(void)
{
  LOG_CLEAN("== Testing monitor destroy unadopts before dropping the machines");

  hub_init();
  monitor_list_init();

  g_unadopt_seen_machine = NULL;
  hub_register_component(&g_observer_component);
  assert_or_abort(g_observer_component.registered == true);

  Monitor* m = monitor_create(121);
  assert_or_abort(m != NULL);

  uint32_t     states[] = { 0, 1 };
  SMTransition trans[]  = {
    { .from_state = 0, .to_state = 1 },
  };
  SMTemplate*   tmpl = sm_template_create("probe", states, 2, trans, 1, 0);
  StateMachine* sm   = sm_create(m, tmpl, NULL, NULL);
  assert_or_abort(tmpl != NULL && sm != NULL);
  assert(monitor_set_sm(m, "probe", sm) == true);

  monitor_destroy(m);

  /* The hook saw the machine, and the monitor freed it afterwards. */
  assert(g_unadopt_seen_machine == sm);

  monitor_list_shutdown();
  sm_template_destroy(tmpl);
  hub_unregister_component(g_observer_component.name);
  hub_shutdown();
}

/*
 * Test: removing the machine stored under a name empties its slot.
 *
 * A monitor that kept the emptied slot would start a second entry for the same
 * name on the next set, and the machine the removal replaced would never be
 * freed.
 */
void
test_monitor_set_sm_null_removes_entry(void)
{
  LOG_CLEAN("== Testing monitor_set_sm removes the machine it replaces");

  hub_init();
  monitor_list_init();

  Monitor* m = monitor_create(120);
  assert_or_abort(m != NULL);

  uint32_t     states[] = { 0, 1 };
  SMTransition trans[]  = {
    { .from_state = 0, .to_state = 1 },
  };
  SMTemplate* tmpl = sm_template_create("probe", states, 2, trans, 1, 0);
  assert_or_abort(tmpl != NULL);

  StateMachine* first = sm_create(m, tmpl, NULL, NULL);
  assert_or_abort(first != NULL);
  assert(monitor_set_sm(m, "probe", first) == true);
  assert(monitor_get_sm(m, "probe") == first);
  assert(m->sms.count == 1);

  assert(monitor_set_sm(m, "probe", NULL) == true);
  assert(monitor_get_sm(m, "probe") == NULL);
  assert(m->sms.count == 0);

  /* Setting the name again starts from an empty table. */
  StateMachine* second = sm_create(m, tmpl, NULL, NULL);
  assert_or_abort(second != NULL);
  assert(monitor_set_sm(m, "probe", second) == true);
  assert(monitor_get_sm(m, "probe") == second);
  assert(m->sms.count == 1);

  /* Removing a name that was never set is not an error. */
  assert(monitor_set_sm(m, "absent", NULL) == true);
  assert(m->sms.count == 1);

  monitor_destroy(m);
  monitor_list_shutdown();
  sm_template_destroy(tmpl);
  hub_shutdown();
}

void
test_monitor_create_destroy(void)
{
  LOG_CLEAN("== Testing monitor create and destroy");

  hub_init();
  monitor_list_init();

  /* Create a monitor */
  xcb_randr_output_t output = 100;
  Monitor*           m      = monitor_create(output);

  if (m == NULL) {
    LOG_ERROR("monitor_create failed");
    abort();
  }
  assert(m != NULL);
  assert(m->target.id == (TargetID) output);
  assert(m->target.type_id == hub_get_target_type_id_by_name("monitor"));
  assert(m->target.registered == true);
  assert(m->output == output);
  assert(m->crtc == XCB_NONE);
  assert(m->x == 0);
  assert(m->y == 0);
  assert(m->width == 0);
  assert(m->height == 0);
  assert(m->next == NULL);

  /* Verify it's registered with hub */
  HubTarget* t = hub_get_target_by_id((TargetID) output);
  if (t == NULL) {
    LOG_ERROR("hub_get_target_by_id failed");
    abort();
  }
  assert(t != NULL);
  assert(t->type_id == hub_get_target_type_id_by_name("monitor"));

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
  /* NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) */
  Monitor* m1 = monitor_create(100);
  /* NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) */
  Monitor* m2 = monitor_create(200);
  /* NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) */
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

  /* Set geometry via accessor */
  monitor_set_geometry(m, 1920, 1080, 1920, 1080);

  assert(m->x == 1920);
  assert(m->y == 1080);
  assert(m->width == 1920);
  assert(m->height == 1080);

  /* Set position via accessor */
  monitor_set_geometry(m, 0, 0, 1920, 1080);
  assert(m->x == 0);
  assert(m->y == 0);

  /* Clean up */
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
  monitor_set_geometry(m1, 0, 0, 1920, 1080);

  Monitor* m2 = monitor_create(200); /* Secondary */
  monitor_set_geometry(m2, 1920, 0, 1920, 1080);

  Monitor* m3 = monitor_create(300); /* Tertiary */
  monitor_set_geometry(m3, 0, 1080, 3840, 2160);

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

  /* Try to create another with same output - should fail due to duplicate ID */
  Monitor* m2 = monitor_create(100);

  /* m2 should be NULL due to hub rejecting duplicate ID */
  assert(m2 == NULL);

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
  static RequestType monitor_requests[]     = { 1, 0 };
  static const char* monitor_target_names[] = { "monitor", NULL };

  static HubComponent test_component = {
    .name                  = "test-monitor-component",
    .requests              = monitor_requests,
    .accepted_target_names = monitor_target_names,
    .accepted_targets      = NULL,
    .registered            = false,
  };

  hub_register_component(&test_component);
  assert(test_component.registered == true);

  /* Create a monitor - it should be registered with hub */
  Monitor* m = monitor_create(100);
  assert(hub_get_target_by_id(100) != NULL);

  /* Get components for monitor type */
  HubTarget** monitors = hub_get_targets_by_type(TARGET_TYPE_MONITOR);
  if (monitors == NULL) {
    LOG_ERROR("hub_get_targets_by_type returned NULL");
    abort();
  }
  assert(monitors != NULL);
  assert(monitors[0] == &m->target);
  assert(monitors[1] == NULL); /* NULL-terminated */

  /* Verify client type is separate */
  HubTarget** clients = hub_get_targets_by_type(TARGET_TYPE_CLIENT);
  if (clients == NULL) {
    LOG_ERROR("hub_get_targets_by_type returned NULL");
    abort();
  }
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
  test_monitor_geometry();
  test_monitor_multiple_monitors();
  test_monitor_double_create();
  test_monitor_null_destroy();
  test_monitor_with_hub_integration();
  test_monitor_set_sm_null_removes_entry();
  test_monitor_destroy_unadopts_before_freeing_machines();
});
