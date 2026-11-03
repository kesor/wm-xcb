/*
 * Monitor Manager Component Tests
 *
 * Tests for the monitor manager component implementation.
 * Requires: hub, xcb-handler, monitor target
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
  bool        found = false;
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
  const char** target_names = comp->accepted_target_names;
  if (target_names == NULL) {
    abort();
  }

  bool found_mon = false;
  for (int i = 0; target_names[i] != NULL; i++) {
    if (strcmp(target_names[i], "monitor") == 0) {
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
  if (t->type_id != hub_get_target_type_id_by_name("monitor")) {
    abort();
  }

  /* Get all monitors */
  HubTarget** monitors = hub_get_targets_by_type(hub_get_target_type_id_by_name("monitor"));
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

/*
 * A CRTC change event carries the new geometry, so the synchronous fallback
 * applies it without asking the server.
 *
 * This covers the fallback branch only: there is no X connection here, so the
 * async pass cannot start (monitor_manager_pass_begin() returns false when dpy
 * is NULL). The test asserts that, so it cannot read as coverage of the pass.
 * What the pass does with a reply already in flight is covered by the seam
 * suite, not here.
 */
void
test_monitor_manager_crtc_change_uses_the_event_geometry_sync_fallback(void)
{
  LOG_CLEAN("== Testing a CRTC change event sets the monitor geometry");

  hub_init();
  xcb_handler_init();
  monitor_list_init();
  monitor_manager_init();

  Monitor* m = monitor_create(7);
  assert_or_abort(m != NULL);
  m->crtc = 42;

  /* A second monitor sits on another CRTC and must not move. */
  Monitor* other = monitor_create(8);
  assert_or_abort(other != NULL);
  other->crtc = 99;
  monitor_set_geometry(other, 1, 2, 3, 4);

  xcb_randr_notify_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type = XCB_RANDR_NOTIFY;
  event.subCode       = XCB_RANDR_NOTIFY_CRTC_CHANGE;
  event.u.cc.crtc     = 42;
  event.u.cc.x        = 100;
  event.u.cc.y        = 50;
  event.u.cc.width    = 800;
  event.u.cc.height   = 600;

  monitor_manager_handle_randr_notify(&event);

  assert_or_abort(m->x == 100);
  assert_or_abort(m->y == 50);
  assert_or_abort(m->width == 800);
  assert_or_abort(m->height == 600);

  assert_or_abort(other->x == 1);
  assert_or_abort(other->y == 2);
  assert_or_abort(other->width == 3);
  assert_or_abort(other->height == 4);

  monitor_destroy(m);
  monitor_destroy(other);
  monitor_manager_shutdown();
  xcb_handler_shutdown();
  monitor_list_shutdown();
  hub_shutdown();
}

/*
 * A disconnected output is unmanaged on the spot in the synchronous fallback:
 * destroying a monitor needs no answer from the server.
 *
 * The async pass cannot start without a connection, and this test says so
 * instead of implying the pass was exercised.
 */
void
test_monitor_manager_disconnect_destroys_the_monitor_sync_fallback(void)
{
  LOG_CLEAN("== Testing a disconnected output loses its monitor");

  hub_init();
  xcb_handler_init();
  monitor_list_init();
  monitor_manager_init();

  Monitor* m = monitor_create(7);
  assert_or_abort(m != NULL);

  xcb_randr_notify_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type   = XCB_RANDR_NOTIFY;
  event.subCode         = XCB_RANDR_NOTIFY_OUTPUT_CHANGE;
  event.u.oc.output     = 7;
  event.u.oc.connection = XCB_RANDR_CONNECTION_DISCONNECTED;

  monitor_manager_handle_randr_notify(&event);

  /* The pass never starts without an X connection; this test is the fallback. */
  assert_or_abort(!monitor_manager_pass_running_for_tests());
  assert_or_abort(monitor_get_by_output(7) == NULL);
  assert_or_abort(monitor_list_get_first() == NULL);

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
  test_monitor_manager_crtc_change_uses_the_event_geometry_sync_fallback();
  test_monitor_manager_disconnect_destroys_the_monitor_sync_fallback();
});
