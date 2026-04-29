/*
 * Monitor Manager Component Tests
 *
 * Tests for the monitor manager component implementation.
 * Requires: hub, xcb-handler, monitor target
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <assert.h>
#include <stdlib.h>

#include "src/components/monitor-manager.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "test-wm.h"
#include "wm-hub.h"

void
test_monitor_manager_component_init_shutdown(void)
{
  LOG_CLEAN("== Testing monitor manager init and shutdown");

  hub_init();
  xcb_handler_init();
  monitor_list_init();

  /* Should be able to init the monitor manager */
  monitor_manager_init();

  /* Component should be registered with hub */
  HubComponent* comp = hub_get_component_by_name("monitor-manager");
  assert(comp != NULL);
  assert(comp->registered == true);

  /* Should be able to shutdown */
  monitor_manager_shutdown();
  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_manager_handler_registration(void)
{
  LOG_CLEAN("== Testing monitor manager handler registration");

  hub_init();
  xcb_handler_init();
  monitor_list_init();

  monitor_manager_init();

  /* Verify RandR handler is registered */
  XCBHandler* handlers = xcb_handler_lookup(XCB_RANDR_NOTIFY);
  assert(handlers != NULL);

  /* Should be able to find our handler */
  bool        found = false;
  XCBHandler* h;
  for (h = handlers; h != NULL; h = xcb_handler_next(h)) {
    if (h->component == &monitor_manager_component) {
      found = true;
      break;
    }
  }
  assert(found == true);

  /* Shutdown */
  monitor_manager_shutdown();
  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_manager_multiple_init(void)
{
  LOG_CLEAN("== Testing monitor manager multiple init is safe");

  hub_init();
  xcb_handler_init();
  monitor_list_init();

  /* Init once */
  monitor_manager_init();
  HubComponent* comp1 = hub_get_component_by_name("monitor-manager");
  assert(comp1 != NULL);

  /* Second init should be idempotent or safe */
  monitor_manager_init();
  HubComponent* comp2 = hub_get_component_by_name("monitor-manager");
  assert(comp2 == comp1); /* Same component */

  /* Shutdown once */
  monitor_manager_shutdown();

  /* Shutdown again should be safe */
  monitor_manager_shutdown();

  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_manager_no_requests(void)
{
  LOG_CLEAN("== Testing monitor manager has no requests");

  hub_init();
  xcb_handler_init();
  monitor_list_init();

  monitor_manager_init();

  /* Monitor manager should have empty requests array (no requests handled) */
  HubComponent* comp = hub_get_component_by_name("monitor-manager");
  assert(comp != NULL);
  assert(comp->requests != NULL);
  assert(comp->requests[0] == 0); /* 0-terminated, empty */

  /* Sending a request should be handled gracefully */
  hub_send_request(999, 100); /* Non-existent request type */

  /* Shutdown */
  monitor_manager_shutdown();
  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_manager_accepts_monitor_target(void)
{
  LOG_CLEAN("== Testing monitor manager accepts MONITOR target type");

  hub_init();
  xcb_handler_init();
  monitor_list_init();

  monitor_manager_init();

  /* Verify component accepts MONITOR target type */
  HubComponent* comp = hub_get_component_by_name("monitor-manager");
  assert(comp != NULL);
  assert(comp->targets != NULL);

  bool found_mon = false;
  for (int i = 0; comp->targets[i] != TARGET_TYPE_NONE; i++) {
    if (comp->targets[i] == TARGET_TYPE_MONITOR) {
      found_mon = true;
      break;
    }
  }
  assert(found_mon == true);

  /* Shutdown */
  monitor_manager_shutdown();
  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

void
test_monitor_manager_with_monitors(void)
{
  LOG_CLEAN("== Testing monitor manager with monitor targets");

  hub_init();
  xcb_handler_init();
  monitor_list_init();
  monitor_manager_init();

  /* Create a monitor */
  Monitor* m = monitor_create(100);
  assert(m != NULL);
  assert(hub_get_target_by_id(100) != NULL);

  /* Verify it's a monitor target */
  HubTarget* t = hub_get_target_by_id(100);
  assert(t->type == TARGET_TYPE_MONITOR);

  /* Get all monitors */
  HubTarget** monitors = hub_get_targets_by_type(TARGET_TYPE_MONITOR);
  assert(monitors != NULL);
  assert(monitors[0] != NULL);
  assert(monitors[1] == NULL); /* NULL-terminated */

  /* Shutdown */
  monitor_destroy(m);
  monitor_manager_shutdown();
  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

TEST_GROUP(MonitorManager, {
  test_monitor_manager_component_init_shutdown();
  test_monitor_manager_handler_registration();
  test_monitor_manager_multiple_init();
  test_monitor_manager_no_requests();
  test_monitor_manager_accepts_monitor_target();
  test_monitor_manager_with_monitors();
});
