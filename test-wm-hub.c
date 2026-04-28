#include "test-wm.h"
#include "wm-hub.h"

/* Test component definitions */
static RequestType fullscreen_requests[] = { 1, 2, 0 };   /* REQ_FULLSCREEN_ENTER = 1, REQ_FULLSCREEN_EXIT = 2 */
static TargetType  client_targets[] = { TARGET_TYPE_CLIENT, 0 };
static TargetType  monitor_targets[] = { TARGET_TYPE_MONITOR, 0 };

static HubComponent test_fullscreen = {
  .name       = "fullscreen",
  .requests   = fullscreen_requests,
  .targets    = client_targets,
  .registered = false,
};

static HubComponent test_focus = {
  .name       = "focus",
  .requests   = (RequestType[]){ 3, 0 },  /* REQ_CLIENT_FOCUS = 3 */
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

  /* Request type 1 should map to fullscreen component */
  HubComponent* found = hub_get_component_by_request_type(1);
  assert(found == &test_fullscreen);

  /* Request type 3 should map to focus component */
  found = hub_get_component_by_request_type(3);
  assert(found == &test_focus);

  /* Unregister fullscreen - focus should now handle type 1 */
  hub_unregister_component("fullscreen");
  found = hub_get_component_by_request_type(1);
  assert(found == &test_focus);

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

int
main(void)
{
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

  LOG_CLEAN("== All hub tests passed!");
  return 0;
}
