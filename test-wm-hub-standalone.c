/*
 * Standalone test for wm-hub event bus
 * Compiles without XCB dependencies
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Mock wm-running.h
 */
int running = 1;

#include "wm-hub.h"

#define assert(EXPRESSION)                                         \
  if (!(EXPRESSION)) {                                             \
    printf("%s:%d: %s - FAIL\n", __FILE__, __LINE__, #EXPRESSION); \
    exit(1);                                                       \
  } else {                                                         \
    printf("%s:%d: %s - pass\n", __FILE__, __LINE__, #EXPRESSION); \
  }

#define LOG_CLEAN(pFormat, ...)     \
  {                                 \
    printf(pFormat, ##__VA_ARGS__); \
    printf("\n");                   \
  }

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
 * Test handlers
 */
static void
handler_a(struct Event* e)
{
  handler_a_calls++;
  last_target = e->target;
  last_data   = e->data;
  printf("  handler_a called: type=%u target=%u data=%p\n", e->type, e->target, e->data);
}

static void
handler_b(struct Event* e)
{
  handler_b_calls++;
  last_target = e->target;
  printf("  handler_b called: type=%u target=%u\n", e->type, e->target);
}

static void
handler_c(struct Event* e)
{
  handler_c_calls++;
  printf("  handler_c called: type=%u target=%u\n", e->type, e->target);
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

  int handler_call_count = 0;

  void
      handler_check_data(struct Event * e)
  {
    handler_call_count++;
    /* userdata is stored per-subscriber, event data is separate */
    /* For this test, we just verify the handler is called correctly */
    (void) e;
  }

  handler_call_count = 0;

  int my_userdata = 99;
  hub_subscribe(EVT_TEST_A, handler_check_data, &my_userdata);
  hub_emit(EVT_TEST_A, 1, NULL);

  assert(handler_call_count == 1);
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
  printf("=== Hub Event Bus Tests ===\n\n");

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

  printf("\n=== All tests passed ===\n");
  return 0;
}
