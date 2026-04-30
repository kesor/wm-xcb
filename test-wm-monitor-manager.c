/*
 * Monitor Manager Component Tests
 *
 * Tests for the monitor manager component implementation.
 * Requires: hub, xcb-handler, monitor target
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <assert.h>
#include <stdlib.h>

#include "test-wm.h"
#include "wm-hub.h"
#include "src/xcb/xcb-handler.h"
#include "src/components/monitor-manager.h"
#include "src/target/monitor.h"

void
test_monitor_manager_component_init_shutdown(void)
{
  LOG_CLEAN("== Testing monitor manager init and shutdown");

  hub_init();
  xcb_handler_init();
  monitor_list_init();

  /* Should be able to init the monitor manager */
  monitor_manager_init();

  /* Component should be registered with hub.
   * Use local variable to help static analyzer track non-NULL state. */
  HubComponent* comp = hub_get_component_by_name("monitor-manager");
  if (comp == NULL) {
    abort();
  }
  if (!comp->registered) {
    abort();
  }

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
  if (handlers == NULL) {
    abort();
  }

  /* Should be able to find our handler */
  bool found = false;
  XCBHandler* h;
  for (h = handlers; h != NULL; h = xcb_handler_next(h)) {
    if (h->component == &monitor_manager_component) {
      found = true;
      break;
    }
  }
  if (!found) {
    abort();
  }

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
  if (comp1 == NULL) {
    abort();
  }

  /* Second init should be idempotent or safe */
  monitor_manager_init();
  HubComponent* comp2 = hub_get_component_by_name("monitor-manager");
  if (comp2 == NULL) {
    abort();
  }
  if (comp2 != comp1) {
    abort();
  }

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
  if (comp == NULL) {
    abort();
  }
  RequestType* req = comp->requests;
  if (req == NULL) {
    abort();
  }
  if (req[0] != 0) {
    abort();
  }

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
  if (comp == NULL) {
    abort();
  }
  TargetType* targets = comp->targets;
  if (targets == NULL) {
    abort();
  }

  bool found_mon = false;
  for (int i = 0; targets[i] != TARGET_TYPE_NONE; i++) {
    if (targets[i] == TARGET_TYPE_MONITOR) {
      found_mon = true;
      break;
    }
  }
  if (!found_mon) {
    abort();
  }

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
  if (m == NULL) {
    abort();
  }
  if (hub_get_target_by_id(100) == NULL) {
    abort();
  }

  /* Verify it's a monitor target */
  HubTarget* t = hub_get_target_by_id(100);
  if (t->type != TARGET_TYPE_MONITOR) {
    abort();
  }

  /* Get all monitors */
  HubTarget** monitors = hub_get_targets_by_type(TARGET_TYPE_MONITOR);
  if (monitors == NULL) {
    abort();
  }
  if (monitors[0] == NULL) {
    abort();
  }
  if (monitors[1] != NULL) {
    abort();
  }

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
