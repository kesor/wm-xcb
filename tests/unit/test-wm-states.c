/*
 * Tests for the input state machine in wm-states.c.
 *
 * handle_state_event() is what the event loop runs on every X event: it copies
 * the event's fields into the context the transition table reads, works out
 * which window the pointer is over, and runs the state function for whatever
 * state the table lands on. These tests drive real X event structs through it
 * and assert the context and the state function that ran.
 */

#include <string.h>

#include "src/core/wm-states.h"
#include "src/core/wm-xcb.h"
#include "test-registry.h"
#include "test-wm.h"

#define TEST_ROOT 0x5A5A

static int idle_calls   = 0;
static int move_calls   = 0;
static int resize_calls = 0;

static void
counting_idle(struct Context* state_ctx)
{
  (void) state_ctx;
  idle_calls++;
}

static void
counting_move(struct Context* state_ctx)
{
  (void) state_ctx;
  move_calls++;
}

static void
counting_resize(struct Context* state_ctx)
{
  (void) state_ctx;
  resize_calls++;
}

/*
 * A known context: the global one carries whatever the last test left in it, and
 * the state functions are replaced so a test can tell which one ran.
 */
static void
reset_context(void)
{
  idle_calls   = 0;
  move_calls   = 0;
  resize_calls = 0;

  ctx.state                            = STATE_IDLE;
  ctx.state_funcs[STATE_IDLE]          = counting_idle;
  ctx.state_funcs[STATE_WINDOW_MOVE]   = counting_move;
  ctx.state_funcs[STATE_WINDOW_RESIZE] = counting_resize;

  ctx.window_target_type = WndTypeNone;
  ctx.event_type         = EventTypeNone;
  ctx.key_mod            = KeyNone;
  ctx.key                = XCB_NONE;
  ctx.btn_mod            = KeyNone;
  ctx.btn                = XCB_NONE;
  ctx.root               = XCB_NONE;
  ctx.root_x             = XCB_NONE;
  ctx.root_y             = XCB_NONE;
  ctx.child              = XCB_NONE;
  ctx.event_x            = XCB_NONE;
  ctx.event_y            = XCB_NONE;

  root = TEST_ROOT;
}

void
test_key_press_fills_the_context(void)
{
  LOG_CLEAN("== Testing a key press reaches the context the table reads");

  reset_context();

  xcb_key_press_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type = XCB_KEY_PRESS;
  event.state         = XCB_KEY_BUT_MASK_MOD_1;
  event.detail        = 38;
  event.root          = 0x42;
  event.root_x        = 11;
  event.root_y        = 12;
  event.child         = 0x99;
  event.same_screen   = 1;
  event.event_x       = 3;
  event.event_y       = 4;

  handle_state_event((xcb_generic_event_t*) &event);

  assert(ctx.event_type == EventTypeKeyPress);
  assert(ctx.key_mod == KeyMod1);
  assert(ctx.key == 38);
  assert(ctx.root == 0x42);
  assert(ctx.root_x == 11);
  assert(ctx.root_y == 12);
  assert(ctx.child == 0x99);
  assert(ctx.event_x == 3);
  assert(ctx.event_y == 4);
  assert(ctx.state == STATE_IDLE);
  assert(idle_calls == 1);

  root = XCB_NONE;
}

/*
 * A press in another window on the same screen is still ours; one on another
 * screen has no position in our coordinates, so the one already there stays.
 */
void
test_key_release_without_same_screen_keeps_the_local_position(void)
{
  LOG_CLEAN("== Testing an event from another screen leaves the local position");

  reset_context();
  ctx.event_x = 77;
  ctx.event_y = 78;

  xcb_key_release_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type = XCB_KEY_RELEASE;
  event.detail        = 24;
  event.same_screen   = 0;

  handle_state_event((xcb_generic_event_t*) &event);

  assert(ctx.event_type == EventTypeKeyRelease);
  assert(ctx.key == 24);
  assert(ctx.event_x == 77);
  assert(ctx.event_y == 78);
  assert(idle_calls == 1);

  root = XCB_NONE;
}

void
test_button_press_fills_the_button_context(void)
{
  LOG_CLEAN("== Testing a button press reaches the context the table reads");

  reset_context();

  xcb_button_press_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type = XCB_BUTTON_PRESS;
  event.state         = XCB_KEY_BUT_MASK_SHIFT;
  event.detail        = 1;
  event.root          = 0x42;
  event.root_x        = 21;
  event.root_y        = 22;
  event.child         = 0x99;
  event.same_screen   = 1;
  event.event_x       = 5;
  event.event_y       = 6;

  handle_state_event((xcb_generic_event_t*) &event);

  assert(ctx.event_type == EventTypeBtnPress);
  assert(ctx.btn_mod == KeyShift);
  assert(ctx.btn == 1);
  assert(ctx.root_x == 21);
  assert(ctx.root_y == 22);
  assert(ctx.child == 0x99);
  assert(ctx.event_x == 5);
  assert(ctx.event_y == 6);
  assert(idle_calls == 1);

  /* The window type is worked out from the event in hand, and a press names no
   * window the table can match: it is the enter that names one. The state
   * therefore stays put. */
  assert(ctx.window_target_type == WndTypeNone);
  assert(ctx.state == STATE_IDLE);

  root = XCB_NONE;
}

void
test_enter_notify_names_the_window_under_the_pointer(void)
{
  LOG_CLEAN("== Testing an enter notify names the window under the pointer");

  xcb_enter_notify_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type = XCB_ENTER_NOTIFY;

  reset_context();
  event.event = 0x1234;
  handle_state_event((xcb_generic_event_t*) &event);
  assert(ctx.window_target_type == WndTypeClient);

  reset_context();
  event.event = TEST_ROOT;
  handle_state_event((xcb_generic_event_t*) &event);
  assert(ctx.window_target_type == WndTypeRoot);

  reset_context();
  event.event = XCB_NONE;
  handle_state_event((xcb_generic_event_t*) &event);
  assert(ctx.window_target_type == WndTypeNone);

  /* An enter is not a key or a button, so no state function runs for it. */
  assert(idle_calls == 0);

  root = XCB_NONE;
}

void
test_other_events_leave_the_state_alone(void)
{
  LOG_CLEAN("== Testing an event the table does not read changes no state");

  reset_context();

  xcb_motion_notify_event_t event;
  memset(&event, 0, sizeof event);
  event.response_type = XCB_MOTION_NOTIFY;

  handle_state_event((xcb_generic_event_t*) &event);

  assert(ctx.event_type == EventTypeNone);
  assert(ctx.window_target_type == WndTypeNone);
  assert(ctx.state == STATE_IDLE);
  assert(idle_calls == 0);

  root = XCB_NONE;
}

TEST_GROUP(InputStateMachine, {
  test_key_press_fills_the_context();
  test_key_release_without_same_screen_keeps_the_local_position();
  test_button_press_fills_the_button_context();
  test_enter_notify_names_the_window_under_the_pointer();
  test_other_events_leave_the_state_alone();
});
