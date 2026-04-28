#include "test-wm.h"
#include "wm-hub.h"

/* Test component definitions - use TARGET_TYPE_NONE for proper termination */
static RequestType fullscreen_requests[] = { 1, 2, 0 };   /* REQ_FULLSCREEN_ENTER = 1, REQ_FULLSCREEN_EXIT = 2 */
static TargetType  client_targets[] = { TARGET_TYPE_CLIENT, TARGET_TYPE_NONE };
static TargetType  monitor_targets[] = { TARGET_TYPE_MONITOR, TARGET_TYPE_NONE };

static HubComponent test_fullscreen = {
  .name       = "fullscreen",
  .requests   = fullscreen_requests,
  .targets    = client_targets,
  .registered = false,
};

/* test_focus handles request type 1 AND 3 (for override/fallback testing) */
static RequestType focus_requests[] = { 1, 3, 0 };   /* REQ_CLIENT_FOCUS = 3, also handles 1 */
static HubComponent test_focus = {
  .name       = "focus",
  .requests   = focus_requests,
  .targets    = client_targets,
  .registered = false,
};

static HubComponent test_monitor = {
  .name       = "monitor",
  .requests   = (RequestType[]){ 4, 5, 0 },  /* REQ_MONITOR_* = 4, 5 */
  .targets    = monitor_targets,
  .registered = false,
};

static HubTarget test_client1 = {
  .id         = 100,
  .type       = TARGET_TYPE_CLIENT,
  .registered = false,
};

static HubTarget test_client2 = {
  .id         = 101,
  .type       = TARGET_TYPE_CLIENT,
  .registered = false,
};

static HubTarget test_monitor1 = {
  .id         = 200,
  .type       = TARGET_TYPE_MONITOR,
  .registered = false,
};

static HubTarget test_monitor2 = {
  .id         = 201,
  .type       = TARGET_TYPE_MONITOR,
  .registered = false,
};

void
test_hub_init_shutdown(void)
{
  LOG_CLEAN("== Testing hub init and shutdown");
  hub_init();
  assert(hub_component_count() == 0);
  assert(hub_target_count() == 0);
  hub_shutdown();
  assert(hub_component_count() == 0);
  assert(hub_target_count() == 0);
}

void
test_register_unregister_component(void)
{
  LOG_CLEAN("== Testing component registration and unregistration");
  hub_init();

  assert(test_fullscreen.registered == false);
  assert(hub_component_count() == 0);

  hub_register_component(&test_fullscreen);
  assert(test_fullscreen.registered == true);
  assert(hub_component_count() == 1);

  hub_register_component(&test_focus);
  assert(test_focus.registered == true);
  assert(hub_component_count() == 2);

  hub_unregister_component("fullscreen");
  assert(test_fullscreen.registered == false);
  assert(hub_component_count() == 1);

  hub_unregister_component("focus");
  assert(test_focus.registered == false);
  assert(hub_component_count() == 0);

  hub_shutdown();
}

void
test_register_null_component(void)
{
  LOG_CLEAN("== Testing registration of NULL component");
  hub_init();
  hub_register_component(NULL);
  assert(hub_component_count() == 0);
  hub_shutdown();
}

void
test_unregister_nonexistent_component(void)
{
  LOG_CLEAN("== Testing unregistration of non-existent component");
  hub_init();
  hub_unregister_component("nonexistent");
  assert(hub_component_count() == 0);
  hub_shutdown();
}

void
test_unregister_null_name(void)
{
  LOG_CLEAN("== Testing unregistration with NULL name");
  hub_init();
  hub_unregister_component(NULL);
  hub_shutdown();
}

void
test_get_component_by_name(void)
{
  LOG_CLEAN("== Testing get component by name");
  hub_init();

  hub_register_component(&test_fullscreen);
  hub_register_component(&test_focus);

  HubComponent* found = hub_get_component_by_name("fullscreen");
  assert(found == &test_fullscreen);

  found = hub_get_component_by_name("focus");
  assert(found == &test_focus);

  found = hub_get_component_by_name("nonexistent");
  assert(found == NULL);

  found = hub_get_component_by_name(NULL);
  assert(found == NULL);

  hub_shutdown();
}

void
test_get_component_by_request_type(void)
{
  LOG_CLEAN("== Testing get component by request type");
  hub_init();

  hub_register_component(&test_fullscreen);
  hub_register_component(&test_focus);

  /* Request type 1: fullscreen handles it, but focus was registered last
   * and also handles it, so focus wins (last registered wins) */
  HubComponent* found = hub_get_component_by_request_type(1);
  assert(found == &test_focus);

  /* Request type 3: only focus handles it */
  found = hub_get_component_by_request_type(3);
  assert(found == &test_focus);

  /* Unregister focus - request type 1 should now fall back to fullscreen */
  hub_unregister_component("focus");
  found = hub_get_component_by_request_type(1);
  assert(found == &test_fullscreen);   /* fullscreen now handles type 1 */

  /* Unused request type */
  found = hub_get_component_by_request_type(999);
  assert(found == NULL);

  hub_shutdown();
}

void
test_register_unregister_target(void)
{
  LOG_CLEAN("== Testing target registration and unregistration");
  hub_init();

  assert(test_client1.registered == false);
  assert(hub_target_count() == 0);

  hub_register_target(&test_client1);
  assert(test_client1.registered == true);
  assert(hub_target_count() == 1);

  hub_register_target(&test_monitor1);
  assert(test_monitor1.registered == true);
  assert(hub_target_count() == 2);

  hub_unregister_target(100);
  assert(test_client1.registered == false);
  assert(hub_target_count() == 1);

  hub_unregister_target(200);
  assert(test_monitor1.registered == false);
  assert(hub_target_count() == 0);

  hub_shutdown();
}

void
test_register_null_target(void)
{
  LOG_CLEAN("== Testing registration of NULL target");
  hub_init();
  hub_register_target(NULL);
  assert(hub_target_count() == 0);
  hub_shutdown();
}

void
test_register_target_none_id(void)
{
  LOG_CLEAN("== Testing registration of target with NONE ID");
  hub_init();

  HubTarget none_target = {
    .id         = TARGET_ID_NONE,
    .type       = TARGET_TYPE_CLIENT,
    .registered = false,
  };
  hub_register_target(&none_target);
  assert(hub_target_count() == 0);

  hub_shutdown();
}

void
test_unregister_nonexistent_target(void)
{
  LOG_CLEAN("== Testing unregistration of non-existent target");
  hub_init();
  hub_unregister_target(999);
  assert(hub_target_count() == 0);
  hub_shutdown();
}

void
test_unregister_none_id(void)
{
  LOG_CLEAN("== Testing unregistration with NONE ID");
  hub_init();
  hub_unregister_target(TARGET_ID_NONE);
  hub_shutdown();
}

void
test_get_target_by_id(void)
{
  LOG_CLEAN("== Testing get target by ID");
  hub_init();

  hub_register_target(&test_client1);
  hub_register_target(&test_client2);
  hub_register_target(&test_monitor1);

  HubTarget* found = hub_get_target_by_id(100);
  assert(found == &test_client1);

  found = hub_get_target_by_id(101);
  assert(found == &test_client2);

  found = hub_get_target_by_id(200);
  assert(found == &test_monitor1);

  found = hub_get_target_by_id(999);
  assert(found == NULL);

  found = hub_get_target_by_id(TARGET_ID_NONE);
  assert(found == NULL);

  hub_shutdown();
}

void
test_get_targets_by_type(void)
{
  LOG_CLEAN("== Testing get targets by type");
  hub_init();

  hub_register_target(&test_client1);
  hub_register_target(&test_client2);
  hub_register_target(&test_monitor1);
  hub_register_target(&test_monitor2);

  HubTarget** clients = hub_get_targets_by_type(TARGET_TYPE_CLIENT);
  assert(clients != NULL);
  assert(clients[0] == &test_client1);
  assert(clients[1] == &test_client2);
  assert(clients[2] == NULL);   /* NULL-terminated */

  HubTarget** monitors = hub_get_targets_by_type(TARGET_TYPE_MONITOR);
  assert(monitors != NULL);
  assert(monitors[0] == &test_monitor1);
  assert(monitors[1] == &test_monitor2);
  assert(monitors[2] == NULL);   /* NULL-terminated */

  HubTarget** empty = hub_get_targets_by_type(TARGET_TYPE_KEYBOARD);
  assert(empty != NULL);
  assert(empty[0] == NULL);   /* No keyboard targets registered */

  /* Invalid type */
  HubTarget** invalid = hub_get_targets_by_type(TARGET_TYPE_COUNT + 1);
  assert(invalid == NULL);

  hub_shutdown();
}

void
test_double_registration(void)
{
  LOG_CLEAN("== Testing double registration prevention");
  hub_init();

  hub_register_component(&test_fullscreen);
  assert(hub_component_count() == 1);

  /* Try to register again - should be a no-op */
  hub_register_component(&test_fullscreen);
  assert(hub_component_count() == 1);

  hub_register_target(&test_client1);
  assert(hub_target_count() == 1);

  /* Try to register again - should be a no-op */
  hub_register_target(&test_client1);
  assert(hub_target_count() == 1);

  hub_shutdown();
}

void
test_double_unregistration(void)
{
  LOG_CLEAN("== Testing double unregistration prevention");
  hub_init();

  hub_register_component(&test_fullscreen);
  hub_unregister_component("fullscreen");
  assert(test_fullscreen.registered == false);

  /* Try to unregister again - should fail */
  hub_unregister_component("fullscreen");
  assert(hub_component_count() == 0);

  hub_register_target(&test_client1);
  hub_unregister_target(100);
  assert(test_client1.registered == false);

  /* Try to unregister again - should fail */
  hub_unregister_target(100);
  assert(hub_target_count() == 0);

  hub_shutdown();
}

void
test_idempotent_shutdown(void)
{
  LOG_CLEAN("== Testing idempotent shutdown");
  hub_init();

  hub_register_component(&test_fullscreen);
  hub_register_component(&test_focus);
  hub_register_target(&test_client1);
  hub_register_target(&test_monitor1);

  hub_shutdown();
  assert(hub_component_count() == 0);
  assert(hub_target_count() == 0);
  assert(test_fullscreen.registered == false);
  assert(test_focus.registered == false);
  assert(test_client1.registered == false);
  assert(test_monitor1.registered == false);

  /* Call shutdown again - should be safe */
  hub_shutdown();
  assert(hub_component_count() == 0);
  assert(hub_target_count() == 0);
}

void
test_shutdown_clears_indexes(void)
{
  LOG_CLEAN("== Testing shutdown clears lookup indexes");

  hub_init();
  hub_register_component(&test_fullscreen);
  hub_register_target(&test_client1);

  /* Verify lookups work */
  assert(hub_get_component_by_name("fullscreen") != NULL);
  assert(hub_get_component_by_request_type(1) != NULL);
  assert(hub_get_target_by_id(100) != NULL);

  hub_shutdown();

  /* After shutdown, lookups should return NULL */
  assert(hub_get_component_by_name("fullscreen") == NULL);
  assert(hub_get_component_by_request_type(1) == NULL);
  assert(hub_get_target_by_id(100) == NULL);

  /* Re-init should work correctly */
  hub_init();
  assert(hub_component_count() == 0);
  assert(hub_target_count() == 0);

  hub_shutdown();
}

void
test_duplicate_target_id_rejected(void)
{
  LOG_CLEAN("== Testing duplicate target ID is rejected");
  hub_init();

  hub_register_target(&test_client1);
  assert(hub_target_count() == 1);
  assert(hub_get_target_by_id(100) == &test_client1);

  /* Try to register another target with the same ID - should fail */
  HubTarget duplicate_target = {
    .id         = 100,
    .type       = TARGET_TYPE_MONITOR,
    .registered = false,
  };
  hub_register_target(&duplicate_target);
  assert(hub_target_count() == 1);  /* Should not increase */
  assert(hub_get_target_by_id(100) == &test_client1);  /* Should still be client1 */

  hub_shutdown();
}

void
test_large_target_id_lookup(void)
{
  LOG_CLEAN("== Testing large TargetID values (beyond array bounds)");
  hub_init();

  /* Use a target ID larger than MAX_TARGETS (256) */
  HubTarget large_id_target = {
    .id         = 1000,
    .type       = TARGET_TYPE_CLIENT,
    .registered = false,
  };

  hub_register_target(&large_id_target);
  assert(hub_target_count() == 1);

  /* Should still be lookupable even though id > MAX_TARGETS */
  HubTarget* found = hub_get_target_by_id(1000);
  assert(found == &large_id_target);

  hub_unregister_target(1000);
  assert(hub_target_count() == 0);

  /* After unregister, lookup should return NULL */
  found = hub_get_target_by_id(1000);
  assert(found == NULL);

  hub_shutdown();
}

void
test_target_type_null_termination(void)
{
  LOG_CLEAN("== Testing target type arrays have proper NULL termination");
  hub_init();

  hub_register_target(&test_client1);
  hub_register_target(&test_client2);

  HubTarget** clients = hub_get_targets_by_type(TARGET_TYPE_CLIENT);

  /* Verify proper iteration with NULL terminator */
  int count = 0;
  for (int i = 0; clients[i] != NULL; i++) {
    count++;
    /* Should not exceed our registered count */
    assert(i < 10);  /* Safety limit */
  }
  assert(count == 2);

  hub_shutdown();
}

/*
 * Event Bus Tests
 */

/*
 * Test event types
 */
#define EVT_TEST_A ((EventType) 1)
#define EVT_TEST_B ((EventType) 2)
#define EVT_TEST_C ((EventType) 3)

/*
 * Track handler calls
 */
static int      handler_a_calls = 0;
static int      handler_b_calls = 0;
static int      handler_c_calls = 0;
static TargetID last_target     = TARGET_ID_NONE;
static void*    last_data       = NULL;

/*
 * Test handlers - all static at file scope (not nested)
 */
static void
handler_a(struct Event e)
{
  handler_a_calls++;
  last_target = e.target;
  last_data   = e.data;
  LOG_CLEAN("  handler_a called: type=%u target=%u data=%p", e.type, e.target, e.data);
}

static void
handler_b(struct Event e)
{
  handler_b_calls++;
  last_target = e.target;
  LOG_CLEAN("  handler_b called: type=%u target=%u", e.type, e.target);
}

static void
handler_c(struct Event e)
{
  handler_c_calls++;
  LOG_CLEAN("  handler_c called: type=%u target=%u", e.type, e.target);
}

/*
 * Handler for testing userdata - uses static to track state
 */
static int handler_with_data_call_count = 0;
static int handler_with_data_userdata    = 0;

static void
handler_check_userdata(struct Event e)
{
  handler_with_data_call_count++;
  handler_with_data_userdata = (int) (intptr_t) e.userdata;
}

void
test_basic_subscribe_emit(void)
{
  LOG_CLEAN("== Testing basic subscribe and emit");

  hub_init();

  handler_a_calls = 0;
  hub_subscribe(EVT_TEST_A, handler_a, NULL);

  LOG_CLEAN("  Emitting EVT_TEST_A...");
  hub_emit(EVT_TEST_A, 123, NULL);

  assert(handler_a_calls == 1);
  assert(last_target == 123);
}

void
test_multiple_subscribers(void)
{
  LOG_CLEAN("== Testing multiple subscribers receive same event");

  hub_init();

  handler_a_calls = 0;
  handler_b_calls = 0;
  handler_c_calls = 0;

  hub_subscribe(EVT_TEST_A, handler_a, NULL);
  hub_subscribe(EVT_TEST_A, handler_b, NULL);
  hub_subscribe(EVT_TEST_A, handler_c, NULL);

  LOG_CLEAN("  Emitting EVT_TEST_A...");
  hub_emit(EVT_TEST_A, 456, NULL);

  assert(handler_a_calls == 1);
  assert(handler_b_calls == 1);
  assert(handler_c_calls == 1);
  assert(last_target == 456);
}

void
test_filter_by_event_type(void)
{
  LOG_CLEAN("== Testing subscriber filters by event type");

  hub_init();

  handler_a_calls = 0;
  handler_b_calls = 0;

  hub_subscribe(EVT_TEST_A, handler_a, NULL);
  hub_subscribe(EVT_TEST_B, handler_b, NULL);

  LOG_CLEAN("  Emitting EVT_TEST_A...");
  hub_emit(EVT_TEST_A, 100, NULL);

  assert(handler_a_calls == 1);
  assert(handler_b_calls == 0);    // handler_b should NOT be called

  handler_a_calls = 0;
  handler_b_calls = 0;

  LOG_CLEAN("  Emitting EVT_TEST_B...");
  hub_emit(EVT_TEST_B, 200, NULL);

  assert(handler_a_calls == 0);    // handler_a should NOT be called
  assert(handler_b_calls == 1);
}

void
test_unsubscribe_stops_delivery(void)
{
  LOG_CLEAN("== Testing unsubscribe stops delivery");

  hub_init();

  handler_a_calls = 0;

  hub_subscribe(EVT_TEST_A, handler_a, NULL);

  LOG_CLEAN("  Emitting before unsubscribe...");
  hub_emit(EVT_TEST_A, 1, NULL);
  assert(handler_a_calls == 1);

  hub_unsubscribe(EVT_TEST_A, handler_a);

  LOG_CLEAN("  Emitting after unsubscribe...");
  hub_emit(EVT_TEST_A, 2, NULL);
  assert(handler_a_calls == 1);    // should still be 1, not incremented
}

void
test_unsubscribe_specific_handler(void)
{
  LOG_CLEAN("== Testing unsubscribe only removes specific handler");

  hub_init();

  handler_a_calls = 0;
  handler_b_calls = 0;

  hub_subscribe(EVT_TEST_A, handler_a, NULL);
  hub_subscribe(EVT_TEST_A, handler_b, NULL);

  hub_emit(EVT_TEST_A, 1, NULL);
  assert(handler_a_calls == 1);
  assert(handler_b_calls == 1);

  hub_unsubscribe(EVT_TEST_A, handler_a);

  hub_emit(EVT_TEST_A, 2, NULL);
  assert(handler_a_calls == 1);    // handler_a unchanged
  assert(handler_b_calls == 2);    // handler_b called again
}

void
test_multiple_emit_calls(void)
{
  LOG_CLEAN("== Testing multiple emit calls");

  hub_init();

  handler_a_calls = 0;
  hub_subscribe(EVT_TEST_A, handler_a, NULL);

  hub_emit(EVT_TEST_A, 1, NULL);
  hub_emit(EVT_TEST_A, 2, NULL);
  hub_emit(EVT_TEST_A, 3, NULL);

  assert(handler_a_calls == 3);
  assert(last_target == 3);
}

void
test_data_passed_to_handlers(void)
{
  LOG_CLEAN("== Testing event data is passed to handlers");

  hub_init();

  handler_a_calls = 0;
  last_data       = NULL;

  int test_data = 42;
  hub_subscribe(EVT_TEST_A, handler_a, NULL);
  hub_emit(EVT_TEST_A, 1, &test_data);

  assert(handler_a_calls == 1);
  assert(last_data == &test_data);
}

void
test_userdata_in_subscribe(void)
{
  LOG_CLEAN("== Testing userdata stored per subscription");

  hub_init();

  handler_with_data_call_count = 0;
  handler_with_data_userdata    = 0;

  int my_userdata = 99;
  hub_subscribe(EVT_TEST_A, handler_check_userdata, (void*) (intptr_t) my_userdata);
  hub_emit(EVT_TEST_A, 1, NULL);

  assert(handler_with_data_call_count == 1);
  assert(handler_with_data_userdata == my_userdata);
}

void
test_duplicate_subscribe_rejected(void)
{
  LOG_CLEAN("== Testing duplicate subscribe is rejected");

  hub_init();

  handler_a_calls = 0;

  hub_subscribe(EVT_TEST_A, handler_a, NULL);
  hub_subscribe(EVT_TEST_A, handler_a, NULL);    // duplicate

  hub_emit(EVT_TEST_A, 1, NULL);
  assert(handler_a_calls == 1);                  // should only be called once
}

void
test_unsubscribe_nonexistent_handler(void)
{
  LOG_CLEAN("== Testing unsubscribe of non-subscribed handler is safe");

  hub_init();

  handler_a_calls = 0;

  hub_subscribe(EVT_TEST_A, handler_a, NULL);
  hub_unsubscribe(EVT_TEST_A, handler_b);    // different handler

  hub_emit(EVT_TEST_A, 1, NULL);
  assert(handler_a_calls == 1);              // handler_a still works
}

int
main(void)
{
  LOG_CLEAN("=== Hub Registry Tests ===");

  test_hub_init_shutdown();
  test_register_unregister_component();
  test_register_null_component();
  test_unregister_nonexistent_component();
  test_unregister_null_name();
  test_get_component_by_name();
  test_get_component_by_request_type();
  test_register_unregister_target();
  test_register_null_target();
  test_register_target_none_id();
  test_unregister_nonexistent_target();
  test_unregister_none_id();
  test_get_target_by_id();
  test_get_targets_by_type();
  test_double_registration();
  test_double_unregistration();
  test_idempotent_shutdown();
  test_shutdown_clears_indexes();
  test_duplicate_target_id_rejected();
  test_large_target_id_lookup();
  test_target_type_null_termination();

  LOG_CLEAN("\n=== Hub Event Bus Tests ===");

  test_basic_subscribe_emit();
  test_multiple_subscribers();
  test_filter_by_event_type();
  test_unsubscribe_stops_delivery();
  test_unsubscribe_specific_handler();
  test_multiple_emit_calls();
  test_data_passed_to_handlers();
  test_userdata_in_subscribe();
  test_duplicate_subscribe_rejected();
  test_unsubscribe_nonexistent_handler();

  LOG_CLEAN("\n== All hub tests passed!");
  return 0;
}