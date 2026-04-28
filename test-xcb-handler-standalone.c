/*
 * Standalone test for xcb-handler.c
 * Can be compiled and run without XCB libraries installed
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Minimal stub for wm-log.h */
#define LOG_DEBUG(pFormat, ...) ((void)0)
#define LOG_ERROR(pFormat, ...) ((void)0)
#define LOG_CLEAN(pFormat, ...) ((void)fprintf(stdout, pFormat "\n", ##__VA_ARGS__))

/* Minimal stub for wm-hub.h component type */
typedef struct HubComponent HubComponent;
struct HubComponent {
  const char*    name;
  void*          requests;
  void*          targets;
  bool           registered;
};

/* Include xcb-handler */
#include "src/xcb/xcb-handler.h"

/* Test helper to track handler calls */
static int   handler_call_count   = 0;
static int   last_event_type     = -1;
static void* last_handler_data   = NULL;

/* Mock component for testing */
static HubComponent mock_component = {
  .name      = "test-component",
  .requests  = NULL,
  .targets   = NULL,
  .registered = true,
};

/* Another mock component for multiple handler test */
static HubComponent mock_component2 = {
  .name      = "test-component-2",
  .requests  = NULL,
  .targets   = NULL,
  .registered = true,
};

/* Test handlers */
static void
test_handler_keypress(void* event)
{
  handler_call_count++;
  last_event_type = ((uint8_t*)event)[0];
}

static void
test_handler_button(void* event)
{
  handler_call_count++;
  last_event_type = ((uint8_t*)event)[0];
}

static void
test_handler_data(void* event)
{
  handler_call_count++;
  last_handler_data = event;
}

/* Assertions with messages */
static int assert_count = 0;
static int pass_count   = 0;

static void
check(int condition, const char* msg)
{
  assert_count++;
  if (condition) {
    pass_count++;
    LOG_CLEAN("  [PASS] %s", msg);
  } else {
    LOG_CLEAN("  [FAIL] %s", msg);
  }
}

#define ASSERT(COND, MSG) check((COND), MSG)

void
test_init_shutdown(void)
{
  LOG_CLEAN("== Testing init and shutdown");

  xcb_handler_init();
  ASSERT(xcb_handler_count() == 0, "count is 0 after init");
  xcb_handler_shutdown();
  ASSERT(xcb_handler_count() == 0, "count is 0 after shutdown");
}

void
test_register_single_handler(void)
{
  LOG_CLEAN("== Testing single handler registration");

  xcb_handler_init();

  int result = xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  ASSERT(result == 0, "register returns 0");
  ASSERT(xcb_handler_count() == 1, "count is 1");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_PRESS) == 1, "count for KEY_PRESS is 1");

  xcb_handler_shutdown();
}

void
test_register_multiple_handlers_same_type(void)
{
  LOG_CLEAN("== Testing multiple handlers for same event type");

  xcb_handler_init();

  int r1 = xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  int r2 = xcb_handler_register(XCB_KEY_PRESS, &mock_component2, test_handler_keypress);
  ASSERT(r1 == 0 && r2 == 0, "both registrations return 0");
  ASSERT(xcb_handler_count() == 2, "count is 2");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_PRESS) == 2, "count for KEY_PRESS is 2");

  xcb_handler_shutdown();
}

void
test_register_handlers_different_types(void)
{
  LOG_CLEAN("== Testing handlers for different event types");

  xcb_handler_init();

  int r1 = xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  int r2 = xcb_handler_register(XCB_BUTTON_PRESS, &mock_component, test_handler_button);
  ASSERT(r1 == 0 && r2 == 0, "both registrations return 0");
  ASSERT(xcb_handler_count() == 2, "count is 2");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_PRESS) == 1, "count for KEY_PRESS is 1");
  ASSERT(xcb_handler_count_for_type(XCB_BUTTON_PRESS) == 1, "count for BUTTON_PRESS is 1");
  ASSERT(xcb_handler_count_for_type(XCB_MOTION_NOTIFY) == 0, "count for MOTION_NOTIFY is 0");

  xcb_handler_shutdown();
}

void
test_lookup_handler(void)
{
  LOG_CLEAN("== Testing handler lookup");

  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);

  XCBHandler* h = xcb_handler_lookup(XCB_KEY_PRESS);
  ASSERT(h != NULL, "lookup returns non-NULL for registered type");
  ASSERT(h->handler == test_handler_keypress, "handler function matches");
  ASSERT(h->component == &mock_component, "component matches");

  XCBHandler* h2 = xcb_handler_lookup(XCB_GE_GENERIC);
  ASSERT(h2 == NULL, "lookup returns NULL for unregistered type");

  xcb_handler_shutdown();
}

void
test_lookup_multiple_handlers(void)
{
  LOG_CLEAN("== Testing lookup of multiple handlers");

  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  xcb_handler_register(XCB_KEY_PRESS, &mock_component2, test_handler_keypress);

  XCBHandler* h1 = xcb_handler_lookup(XCB_KEY_PRESS);
  ASSERT(h1 != NULL, "first handler is non-NULL");
  ASSERT(h1->component == &mock_component, "first handler component matches");

  XCBHandler* h2 = xcb_handler_next(h1);
  ASSERT(h2 != NULL, "second handler is non-NULL");
  ASSERT(h2->component == &mock_component2, "second handler component matches");

  XCBHandler* h3 = xcb_handler_next(h2);
  ASSERT(h3 == NULL, "no third handler");

  xcb_handler_shutdown();
}

void
test_dispatch_calls_handler(void)
{
  LOG_CLEAN("== Testing event dispatch calls handler");

  xcb_handler_init();

  handler_call_count = 0;
  last_event_type     = -1;

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);

  /* Create a fake event */
  uint8_t fake_event[32] = {0};
  fake_event[0] = XCB_KEY_PRESS;

  xcb_handler_dispatch(fake_event);

  ASSERT(handler_call_count == 1, "handler was called once");
  ASSERT(last_event_type == XCB_KEY_PRESS, "event type matches");

  xcb_handler_shutdown();
}

void
test_dispatch_calls_all_handlers(void)
{
  LOG_CLEAN("== Testing event dispatch calls all handlers for type");

  xcb_handler_init();

  handler_call_count = 0;

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  xcb_handler_register(XCB_KEY_PRESS, &mock_component2, test_handler_keypress);

  uint8_t fake_event[32] = {0};
  fake_event[0] = XCB_KEY_PRESS;

  xcb_handler_dispatch(fake_event);

  ASSERT(handler_call_count == 2, "both handlers were called");

  xcb_handler_shutdown();
}

void
test_dispatch_no_handler(void)
{
  LOG_CLEAN("== Testing dispatch with no handler registered");

  xcb_handler_init();

  handler_call_count = 0;

  uint8_t fake_event[32] = {0};
  fake_event[0] = XCB_BUTTON_PRESS;

  xcb_handler_dispatch(fake_event);

  ASSERT(handler_call_count == 0, "no handlers called");

  xcb_handler_shutdown();
}

void
test_dispatch_null_event(void)
{
  LOG_CLEAN("== Testing dispatch with NULL event");

  xcb_handler_init();

  /* Should not crash */
  xcb_handler_dispatch(NULL);

  xcb_handler_shutdown();
}

void
test_unregister_component(void)
{
  LOG_CLEAN("== Testing unregister all handlers for component");

  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  xcb_handler_register(XCB_KEY_RELEASE, &mock_component, test_handler_keypress);
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component, test_handler_button);
  xcb_handler_register(XCB_KEY_PRESS, &mock_component2, test_handler_keypress);

  ASSERT(xcb_handler_count() == 4, "count is 4 before unregister");

  xcb_handler_unregister_component(&mock_component);

  ASSERT(xcb_handler_count() == 1, "count is 1 after unregister");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_PRESS) == 1, "KEY_PRESS count is 1");

  XCBHandler* h = xcb_handler_lookup(XCB_KEY_PRESS);
  ASSERT(h != NULL, "KEY_PRESS handler exists");
  ASSERT(h->component == &mock_component2, "remaining handler is for mock_component2");

  xcb_handler_shutdown();
}

void
test_unregister_null_component(void)
{
  LOG_CLEAN("== Testing unregister with NULL component");

  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  ASSERT(xcb_handler_count() == 1, "count is 1");

  xcb_handler_unregister_component(NULL);
  ASSERT(xcb_handler_count() == 1, "count unchanged after NULL unregister");

  xcb_handler_shutdown();
}

void
test_register_null_handler(void)
{
  LOG_CLEAN("== Testing registration with NULL handler");

  xcb_handler_init();

  int result = xcb_handler_register(XCB_KEY_PRESS, &mock_component, NULL);
  ASSERT(result == -1, "returns -1 for NULL handler");
  ASSERT(xcb_handler_count() == 0, "count is 0");

  xcb_handler_shutdown();
}

void
test_register_null_component(void)
{
  LOG_CLEAN("== Testing registration with NULL component");

  xcb_handler_init();

  int result = xcb_handler_register(XCB_KEY_PRESS, NULL, test_handler_keypress);
  ASSERT(result == -1, "returns -1 for NULL component");
  ASSERT(xcb_handler_count() == 0, "count is 0");

  xcb_handler_shutdown();
}

void
test_handler_receives_event_data(void)
{
  LOG_CLEAN("== Testing handler receives complete event data");

  xcb_handler_init();

  handler_call_count = 0;
  last_handler_data  = NULL;

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_data);

  uint8_t fake_event[32] = {0};
  fake_event[0] = XCB_KEY_PRESS;

  xcb_handler_dispatch(fake_event);

  ASSERT(handler_call_count == 1, "handler called once");
  ASSERT(last_handler_data == fake_event, "handler received event data");

  xcb_handler_shutdown();
}

void
test_multiple_components_different_types(void)
{
  LOG_CLEAN("== Testing multiple components handling different event types");

  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  xcb_handler_register(XCB_KEY_RELEASE, &mock_component, test_handler_keypress);
  xcb_handler_register(XCB_BUTTON_PRESS, &mock_component2, test_handler_button);
  xcb_handler_register(XCB_BUTTON_RELEASE, &mock_component2, test_handler_button);

  ASSERT(xcb_handler_count() == 4, "count is 4");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_PRESS) == 1, "KEY_PRESS count is 1");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_RELEASE) == 1, "KEY_RELEASE count is 1");
  ASSERT(xcb_handler_count_for_type(XCB_BUTTON_PRESS) == 1, "BUTTON_PRESS count is 1");
  ASSERT(xcb_handler_count_for_type(XCB_BUTTON_RELEASE) == 1, "BUTTON_RELEASE count is 1");

  XCBHandler* key_h = xcb_handler_lookup(XCB_KEY_PRESS);
  ASSERT(key_h->component == &mock_component, "KEY_PRESS handler is mock_component");

  XCBHandler* btn_h = xcb_handler_lookup(XCB_BUTTON_PRESS);
  ASSERT(btn_h->component == &mock_component2, "BUTTON_PRESS handler is mock_component2");

  xcb_handler_shutdown();
}

void
test_count_after_shutdown(void)
{
  LOG_CLEAN("== Testing count is zero after shutdown");

  xcb_handler_init();

  xcb_handler_register(XCB_KEY_PRESS, &mock_component, test_handler_keypress);
  ASSERT(xcb_handler_count() == 1, "count is 1 before shutdown");

  xcb_handler_shutdown();
  ASSERT(xcb_handler_count() == 0, "count is 0 after shutdown");
  ASSERT(xcb_handler_count_for_type(XCB_KEY_PRESS) == 0, "KEY_PRESS count is 0");

  xcb_handler_init();
  ASSERT(xcb_handler_count() == 0, "count is 0 after re-init");
  xcb_handler_shutdown();
}

int
main(void)
{
  LOG_CLEAN("Starting XCB Handler Registration Tests\n");
  LOG_CLEAN("========================================\n");

  test_init_shutdown();
  test_register_single_handler();
  test_register_multiple_handlers_same_type();
  test_register_handlers_different_types();
  test_lookup_handler();
  test_lookup_multiple_handlers();
  test_dispatch_calls_handler();
  test_dispatch_calls_all_handlers();
  test_dispatch_no_handler();
  test_dispatch_null_event();
  test_unregister_component();
  test_unregister_null_component();
  test_register_null_handler();
  test_register_null_component();
  test_handler_receives_event_data();
  test_multiple_components_different_types();
  test_count_after_shutdown();

  LOG_CLEAN("\n========================================");
  LOG_CLEAN("Results: %d/%d tests passed", pass_count, assert_count);
  if (pass_count == assert_count) {
    LOG_CLEAN("All XCB handler tests passed!\n");
    return 0;
  } else {
    LOG_CLEAN("Some tests FAILED!\n");
    return 1;
  }
}