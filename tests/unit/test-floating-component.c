/*
 * Floating Component Tests
 *
 * Covers the action the default config binds to MOD+Shift+space: that the name
 * is registered with a client target, that the toggle takes a client out of the
 * layout and gives its slot back, and that the layout the tiling component runs
 * leaves a floating client alone.
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <string.h>

#include "src/actions/action-registry.h"
#include "src/actions/keybinding-binding.h"
#include "src/components/client-list.h"
#include "src/components/floating.h"
#include "src/components/focus.h"
#include "src/components/keybinding.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/sm/sm-registry.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"
#include "test-wm.h"
#include "wm-hub.h"

#define TEST_MONITOR_OUTPUT 70
#define TEST_MONITOR_WIDTH  800
#define TEST_MONITOR_HEIGHT 600
#define TEST_MASTER_WIDTH   (TEST_MONITOR_WIDTH / 2)

/* The box a window covers on screen, border included. */
static uint32_t
outer_width(const Client* c)
{
  return (uint32_t) c->width + (uint32_t) c->border_width * 2;
}

static uint32_t
outer_height(const Client* c)
{
  return (uint32_t) c->height + (uint32_t) c->border_width * 2;
}

static void
map_test_window(xcb_window_t window)
{
  xcb_map_request_event_t event = {
    .response_type = XCB_MAP_REQUEST,
    .sequence      = window,
    .parent        = 100,
    .window        = window,
  };
  xcb_handler_dispatch(&event);
}

static Monitor*
create_test_monitor(void)
{
  Monitor* m = monitor_create(TEST_MONITOR_OUTPUT);
  assert_or_abort(m != NULL);
  monitor_set_geometry(m, 0, 0, TEST_MONITOR_WIDTH, TEST_MONITOR_HEIGHT);
  return m;
}

static size_t floated_events = 0;
static size_t tiled_events   = 0;
static size_t failed_events  = 0;

static void
count_floated(struct Event event)
{
  (void) event;
  floated_events++;
}

static void
count_tiled(struct Event event)
{
  (void) event;
  tiled_events++;
}

static void
count_failed(struct Event event)
{
  (void) event;
  failed_events++;
}

/*
 * Test: floating.toggle is registered against the focused client.
 */
void
test_floating_action_registered(void)
{
  LOG_CLEAN("== Testing floating action registration");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  floating_component_init();

  Action* action = action_lookup("floating.toggle");
  assert(action != NULL);
  if (action != NULL) {
    assert(strcmp(action->name, "floating.toggle") == 0);
    assert(action->callback != NULL);
    assert(action->target_type == ACTION_TARGET_CLIENT);
    assert(action->target_required == true);
  }

  floating_component_shutdown();

  /* Shutdown must take the name with it, or a restart cannot register it */
  assert(action_lookup("floating.toggle") == NULL);

  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: registering the action is part of init. Without the registry the
 * component must not come up half-initialized, or a caller that runs before
 * keybinding_init drops floating for the whole session.
 */
void
test_floating_init_requires_action_registry(void)
{
  LOG_CLEAN("== Testing floating init without the action registry");

  hub_init();
  sm_registry_init();
  xcb_handler_init();

  assert(floating_component_init() == false);
  assert(floating_component_is_initialized() == false);
  assert(hub_get_component_by_name(FLOATING_COMPONENT_NAME) == NULL);

  /* The guards and actions the failed init registered must be gone too */
  assert(action_lookup("floating.toggle") == NULL);

  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: floating saves the slot the layout gave the client, and tiling again
 * puts the window back in it. In between, a re-tile leaves the window where it
 * is, which is what lets a floating window keep its size.
 */
void
test_floating_toggle_saves_and_restores_the_geometry(void)
{
  LOG_CLEAN("== Testing a float saves the slot and a tile gives it back");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  floating_component_init();
  tiling_component_init();

  create_test_monitor();
  map_test_window(2000);

  Client* c = client_get_by_window(2000);
  assert_or_abort(c != NULL);
  assert(floating_is_floating(c) == false);

  int16_t  tiled_x = c->x;
  int16_t  tiled_y = c->y;
  uint16_t tiled_w = c->width;
  uint16_t tiled_h = c->height;
  assert(outer_width(c) == TEST_MONITOR_WIDTH);

  hub_send_request(REQ_CLIENT_FLOAT, c->target.id);

  assert(floating_is_floating(c) == true);
  const FloatingSavedGeometry* saved = floating_saved_geometry(c);
  assert_or_abort(saved != NULL);
  assert(saved->x == tiled_x);
  assert(saved->y == tiled_y);
  assert(saved->width == tiled_w);
  assert(saved->height == tiled_h);

  /* Something moved the window while it floated. */
  client_set_geometry(c, 111, 222, 300, 200);

  /* A layout pass for the monitor must leave the floating window alone. */
  hub_send_request(REQ_MONITOR_TILE, c->monitor->target.id);
  assert(c->x == 111);
  assert(c->y == 222);
  assert(c->width == 300);
  assert(c->height == 200);

  hub_send_request(REQ_CLIENT_TILE, c->target.id);

  assert(floating_is_floating(c) == false);
  assert(c->x == tiled_x);
  assert(c->y == tiled_y);
  assert(outer_width(c) == TEST_MONITOR_WIDTH);
  assert(outer_height(c) == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  client_list_component_shutdown();
  floating_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: a client that leaves the layout gives its area to the others, and one
 * that comes back takes a slot again. The state is the client's, so the change
 * reaches tiling as an event and not as a call.
 */
void
test_floating_client_leaves_its_area_to_the_others(void)
{
  LOG_CLEAN("== Testing a floating client frees the area it had");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  floating_component_init();
  tiling_component_init();

  create_test_monitor();
  map_test_window(2100);
  map_test_window(2101);

  /* The list holds the newest client first, and that is the master. */
  Client* floating = client_get_by_window(2101);
  Client* other    = client_get_by_window(2100);
  assert_or_abort(floating != NULL && other != NULL);
  assert(outer_width(floating) == TEST_MASTER_WIDTH);

  hub_send_request(REQ_CLIENT_FLOAT, floating->target.id);

  /* The window that is still in the layout has the monitor to itself. */
  assert(outer_width(other) == TEST_MONITOR_WIDTH);
  assert(outer_width(floating) == TEST_MASTER_WIDTH);

  hub_send_request(REQ_CLIENT_TILE, floating->target.id);

  assert(outer_width(floating) == TEST_MASTER_WIDTH);
  assert(outer_width(other) == TEST_MASTER_WIDTH);

  tiling_component_shutdown();
  client_list_component_shutdown();
  floating_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: the size a floating client has survives a tag switch, because the tag
 * change re-tiles the monitor and the layout does not place it.
 */
void
test_floating_size_survives_a_tag_switch(void)
{
  LOG_CLEAN("== Testing a floating client keeps its size across a tag switch");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  tag_list_init();
  client_list_component_init();
  tag_manager_component_init();
  floating_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  map_test_window(2200);

  Client* c = client_get_by_window(2200);
  assert_or_abort(c != NULL);
  client_set_tags(c, TAG_MASK(0));

  hub_send_request(REQ_CLIENT_FLOAT, c->target.id);
  client_set_geometry(c, 111, 222, 300, 200);

  /* Away to an empty tag and back: both switch re-tiles the monitor. */
  tag_manager_set_visible_tags(m, TAG_MASK(1));
  tag_manager_set_visible_tags(m, TAG_MASK(0));

  assert(floating_is_floating(c) == true);
  assert(c->x == 111);
  assert(c->y == 222);
  assert(c->width == 300);
  assert(c->height == 200);

  tiling_component_shutdown();
  floating_component_shutdown();
  tag_manager_component_shutdown();
  client_list_component_shutdown();
  tag_list_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: a request is intent, so the guard on the template's TILED → FLOATING
 * row decides. An unmanaged client has no slot to give back, so the request is
 * refused and the failure is reported instead of the state moving anyway.
 */
void
test_floating_request_refused_for_unmanaged_client(void)
{
  LOG_CLEAN("== Testing a float request for an unmanaged client");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  client_list_init();
  floating_component_init();

  failed_events = 0;
  hub_subscribe(EVT_FLOATING_FAILED, count_failed, NULL);

  Client* c = client_create(2300);
  assert_or_abort(c != NULL);
  assert(client_is_managed(c) == false);

  hub_send_request(REQ_CLIENT_FLOAT, c->target.id);

  assert(floating_is_floating(c) == false);
  assert(failed_events == 1);

  client_list_shutdown();
  floating_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: both transitions announce themselves, so a component that arranges the
 * monitor hears about them without being called.
 */
void
test_floating_events_are_emitted(void)
{
  LOG_CLEAN("== Testing a float and a tile emit their events");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  client_list_init();
  floating_component_init();

  floated_events = 0;
  tiled_events   = 0;
  hub_subscribe(EVT_CLIENT_FLOATED, count_floated, NULL);
  hub_subscribe(EVT_CLIENT_TILED, count_tiled, NULL);

  Client* c = client_create(2400);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);

  hub_send_request(REQ_CLIENT_FLOAT, c->target.id);
  assert(floated_events == 1);
  assert(tiled_events == 0);

  hub_send_request(REQ_CLIENT_TILE, c->target.id);
  assert(tiled_events == 1);
  assert(floated_events == 1);

  client_list_shutdown();
  floating_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: MOD+Shift+space reaches the action through the keybinding table, and
 * the action picks the request that matches the client's state.
 */
void
test_floating_toggle_keybinding(void)
{
  LOG_CLEAN("== Testing MOD+Shift+space toggles floating");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  monitor_list_init();
  keybinding_init();
  client_list_component_init();
  focus_component_init();
  floating_component_init();
  tiling_component_init();

  create_test_monitor();
  map_test_window(2500);

  Client* c = client_get_by_window(2500);
  assert_or_abort(c != NULL);
  client_set_focusable(c, true);
  focus_set_state(c, FOCUS_STATE_FOCUSED);

  assert(floating_is_floating(c) == false);
  assert(keybinding_binding_execute(XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT, 65) == true);
  assert(floating_is_floating(c) == true);

  assert(keybinding_binding_execute(XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT, 65) == true);
  assert(floating_is_floating(c) == false);

  tiling_component_shutdown();
  floating_component_shutdown();
  focus_component_shutdown();
  client_list_component_shutdown();
  keybinding_shutdown();
  monitor_list_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

TEST_GROUP(FloatingComponent, {
  test_floating_action_registered();
  test_floating_init_requires_action_registry();
  test_floating_toggle_saves_and_restores_the_geometry();
  test_floating_client_leaves_its_area_to_the_others();
  test_floating_size_survives_a_tag_switch();
  test_floating_request_refused_for_unmanaged_client();
  test_floating_events_are_emitted();
  test_floating_toggle_keybinding();
});
