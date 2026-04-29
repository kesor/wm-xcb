#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"

/* Mock component for testing */
static HubComponent mock_component = {
  .name       = "test-component",
  .requests   = (RequestType[]) { 0 },
  .targets    = (TargetType[]) { TARGET_TYPE_NONE },
  .registered = false,
};

static HubComponent mock_component2 = {
  .name       = "test-component-2",
  .requests   = (RequestType[]) { 0 },
  .targets    = (TargetType[]) { TARGET_TYPE_NONE },
  .registered = false,
};

/* Track handler calls */
static int   handler_call_count = 0;
static void* last_handler_event = NULL;

static void
test_handler(void* event)
{
  handler_call_count++;
  last_handler_event = event;
}

static void
test_handler2(void* event)
{
  handler_call_count++;
}

static void
reset_handler_state(void)
{
  handler_call_count = 0;
  last_handler_event = NULL;
}

void
test_xcb_handler_init_shutdown(void)
{
  LOG_CLEAN("== Testing xcb_handler init and shutdown");

  /* Initialize the hub first to keep test setup consistent. */
  hub_init();

  xcb_handler_init();
  assert(xcb_handler_count() == 0);

  xcb_handler_shutdown();

  hub_shutdown();
}

void
test_register_single_handler(void)
{
  LOG_CLEAN("== Testing single handler registration");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  int result = xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);
  assert(result == 0);
  assert(xcb_handler_count() == 1);
  assert(xcb_handler_count_for_type(XCB_KEY_PRESS) == 1);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_register_multiple_handlers_same_event(void)
{
  LOG_CLEAN("== Testing multiple handlers for same event type");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  /* Register two handlers for the same event type */
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component, test_handler);
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component2, test_handler2);

  assert(xcb_handler_count() == 2);
  assert(xcb_handler_count_for_type(XCB_BUTTON_PRESS) == 2);

  /* Verify we can look up handlers */
  XCBHandler* h = xcb_handler_lookup(XCB_BUTTON_PRESS);
  assert(h != NULL);
  assert(h->handler == test_handler);

  XCBHandler* h2 = xcb_handler_next(h);
  assert(h2 != NULL);
  assert(h2->handler == test_handler2);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_register_different_event_types(void)
{
  LOG_CLEAN("== Testing registration for different event types");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);
  xcb_handler_register(XCB_KEY_RELEASE, &mock_component, test_handler2);
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component, test_handler);

  assert(xcb_handler_count() == 3);
  assert(xcb_handler_count_for_type(XCB_KEY_PRESS) == 1);
  assert(xcb_handler_count_for_type(XCB_KEY_RELEASE) == 1);
  assert(xcb_handler_count_for_type(XCB_BUTTON_PRESS) == 1);

  /* No handler for unregister event */
  assert(xcb_handler_count_for_type(XCB_MOTION_NOTIFY) == 0);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_dispatch_calls_handlers(void)
{
  LOG_CLEAN("== Testing dispatch calls registered handlers");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);

  /* Create a fake event structure - response_type is first byte */
  uint8_t fake_event[32] = { XCB_KEY_PRESS, 0 };

  xcb_handler_dispatch(fake_event);

  assert(handler_call_count == 1);
  assert(last_handler_event == fake_event);

  /* Dispatch again - should call handler again */
  xcb_handler_dispatch(fake_event);
  assert(handler_call_count == 2);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_dispatch_multiple_handlers(void)
{
  LOG_CLEAN("== Testing dispatch calls all handlers for event type");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component, test_handler);
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component2, test_handler2);

  uint8_t fake_event[32] = { XCB_BUTTON_PRESS, 0 };

  xcb_handler_dispatch(fake_event);

  /* Both handlers should have been called */
  assert(handler_call_count == 2);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_dispatch_no_handler(void)
{
  LOG_CLEAN("== Testing dispatch with no registered handler (no crash)");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  /* No handler registered for KEY_RELEASE */
  uint8_t fake_event[32] = { XCB_KEY_RELEASE, 0 };

  /* Should not crash - just do nothing */
  xcb_handler_dispatch(fake_event);

  assert(handler_call_count == 0);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_dispatch_with_synthetic_event_flag(void)
{
  LOG_CLEAN("== Testing dispatch strips synthetic event flag");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);

  /* Synthetic event has high bit (0x80) set */
  uint8_t synthetic_event[32] = { XCB_KEY_PRESS | 0x80, 0 };

  xcb_handler_dispatch(synthetic_event);

  /* Should still call handler because flag is masked */
  assert(handler_call_count == 1);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_unregister_component(void)
{
  LOG_CLEAN("== Testing unregister all handlers for component");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);
  xcb_handler_register(XCB_KEY_RELEASE, &mock_component, test_handler2);
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component2, test_handler);

  assert(xcb_handler_count() == 3);

  /* Unregister all handlers for mock_component */
  xcb_handler_unregister_component(&mock_component);

  assert(xcb_handler_count() == 1);
  assert(xcb_handler_count_for_type(XCB_KEY_PRESS) == 0);
  assert(xcb_handler_count_for_type(XCB_KEY_RELEASE) == 0);
  assert(xcb_handler_count_for_type(XCB_BUTTON_PRESS) == 1);

  /* Unregister remaining component */
  xcb_handler_unregister_component(&mock_component2);
  assert(xcb_handler_count() == 0);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_xcb_handler_unregister_nonexistent(void)
{
  LOG_CLEAN("== Testing unregister non-existent component is safe");

  hub_init();
  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);
  assert(xcb_handler_count() == 1);

  /* Unregister a component that has no handlers - should be safe */
  xcb_handler_unregister_component(&mock_component2);
  assert(xcb_handler_count() == 1); /* mock_component still registered */

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_unregister_null_component(void)
{
  LOG_CLEAN("== Testing unregister with NULL component is safe");

  hub_init();
  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);
  assert(xcb_handler_count() == 1);

  /* Unregister NULL - should be safe */
  xcb_handler_unregister_component(NULL);
  assert(xcb_handler_count() == 1); /* handler still registered */

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_lookup_returns_null_for_empty(void)
{
  LOG_CLEAN("== Testing lookup returns NULL when no handlers");

  hub_init();
  xcb_handler_init();

  XCBHandler* h = xcb_handler_lookup(XCB_KEY_PRESS);
  assert(h == NULL);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_next_returns_null_at_end(void)
{
  LOG_CLEAN("== Testing xcb_handler_next returns NULL at end of chain");

  hub_init();
  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);

  XCBHandler* h  = xcb_handler_lookup(XCB_KEY_PRESS);
  XCBHandler* h2 = xcb_handler_next(h);

  assert(h != NULL);
  assert(h2 == NULL); /* Only one handler, next should be NULL */

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_register_with_null_handler_fails(void)
{
  LOG_CLEAN("== Testing registration with NULL handler fails");

  hub_init();
  xcb_handler_init();

  int result = xcb_handler_register(XCB_KEY_PRESS, &mock_component, NULL);
  assert(result == -1);
  assert(xcb_handler_count() == 0);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_register_with_null_component_fails(void)
{
  LOG_CLEAN("== Testing registration with NULL component fails");

  hub_init();
  xcb_handler_init();

  int result = xcb_handler_register(XCB_KEY_PRESS, NULL, test_handler);
  assert(result == -1);
  assert(xcb_handler_count() == 0);

  xcb_handler_shutdown();
  hub_shutdown();
}

void
test_dispatch_null_event(void)
{
  LOG_CLEAN("== Testing dispatch with NULL event is safe");

  hub_init();
  xcb_handler_init();

  reset_handler_state();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler);

  xcb_handler_dispatch(NULL);

  assert(handler_call_count == 0); /* Should not call handler */

  xcb_handler_shutdown();
  hub_shutdown();
}

TEST_GROUP(XCBHandler, {
  test_xcb_handler_init_shutdown();
  test_register_single_handler();
  test_register_multiple_handlers_same_event();
  test_register_different_event_types();
  test_dispatch_calls_handlers();
  test_dispatch_multiple_handlers();
  test_dispatch_no_handler();
  test_dispatch_with_synthetic_event_flag();
  test_unregister_component();
  test_xcb_handler_unregister_nonexistent();
  test_unregister_null_component();
  test_lookup_returns_null_for_empty();
  test_next_returns_null_at_end();
  test_register_with_null_handler_fails();
  test_register_with_null_component_fails();
  test_dispatch_null_event();
});