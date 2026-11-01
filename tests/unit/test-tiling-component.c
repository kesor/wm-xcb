/*
 * Tiling Component Tests
 *
 * The tile request has two callers: the client and tag lifecycle code that
 * sends REQ_MONITOR_TILE, and the monitor.tile action a keybinding invokes.
 * These tests drive both and assert the geometry the tiling algorithm
 * promises for the visible clients on a monitor.
 */

#include <string.h>

#include "src/actions/action-registry.h"
#include "src/actions/keybinding-binding.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/keybinding.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"

#define TEST_MONITOR_OUTPUT 70
#define TEST_MONITOR_WIDTH  800
#define TEST_MONITOR_HEIGHT 600
#define TEST_MASTER_WIDTH   (TEST_MONITOR_WIDTH / 2)

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

static void
unmap_test_window(xcb_window_t window)
{
  xcb_unmap_notify_event_t event = {
    .response_type  = XCB_UNMAP_NOTIFY,
    .sequence       = window,
    .event          = window,
    .window         = window,
    .from_configure = 0,
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

/*
 * Test: the action config.wm.def.h names is registered with a monitor target.
 */
void
test_monitor_tile_action_registered(void)
{
  LOG_CLEAN("== Testing monitor.tile action registration");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  tiling_component_init();

  Action* action = action_lookup("monitor.tile");
  assert(action != NULL);
  if (action != NULL) {
    assert(strcmp(action->name, "monitor.tile") == 0);
    assert(action->target_type == ACTION_TARGET_MONITOR);
    assert(action->target_required == true);
    assert(action->callback != NULL);
  }

  tiling_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: a MapRequest tiles the client without anyone calling tiling directly.
 */
void
test_manage_triggers_tiling(void)
{
  LOG_CLEAN("== Testing tiling runs when a client is managed");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  map_test_window(900);

  Client* c = client_get_by_window(900);
  assert(c != NULL);
  if (c != NULL) {
    assert(c->x == m->x);
    assert(c->y == m->y);
    assert(c->width == TEST_MASTER_WIDTH);
    assert(c->height == TEST_MONITOR_HEIGHT);
  }

  tiling_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: removing a client widens the stack left behind.
 */
void
test_unmanage_triggers_tiling(void)
{
  LOG_CLEAN("== Testing tiling runs when a client is unmanaged");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  tiling_component_init();

  create_test_monitor();
  map_test_window(1001);
  map_test_window(1002);
  map_test_window(1003);

  Client* stack = client_get_by_window(1002);
  assert_or_abort(stack != NULL);
  assert(stack->height == TEST_MONITOR_HEIGHT / 2);

  unmap_test_window(1001);

  assert(stack->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: the action a keybinding invokes sends the same request and re-tiles.
 */
void
test_monitor_tile_action_retiles(void)
{
  LOG_CLEAN("== Testing monitor.tile action re-tiles the monitor");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  tiling_component_init();

  create_test_monitor();
  map_test_window(1100);

  Client* c = client_get_by_window(1100);
  assert_or_abort(c != NULL);

  client_set_geometry(c, 7, 9, 123, 45);
  assert(c->width == 123);

  ActionInvocation inv = { .target = TARGET_ID_NONE };
  assert(action_invoke("monitor.tile", &inv) == true);

  assert(c->x == 0);
  assert(c->y == 0);
  assert(c->width == TEST_MASTER_WIDTH);
  assert(c->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: MOD+i reaches the action through the keybinding table.
 */
void
test_monitor_tile_keybinding(void)
{
  LOG_CLEAN("== Testing MOD+i invokes monitor.tile");

  hub_init();
  xcb_handler_init();
  monitor_list_init();
  keybinding_init();
  client_list_component_init();
  tiling_component_init();

  create_test_monitor();
  map_test_window(1200);

  Client* c = client_get_by_window(1200);
  assert_or_abort(c != NULL);

  client_set_geometry(c, 7, 9, 123, 45);
  assert(keybinding_binding_execute(XCB_MOD_MASK_4, 31) == true);
  assert(c->width == TEST_MASTER_WIDTH);
  assert(c->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  client_list_component_shutdown();
  keybinding_shutdown();
  monitor_list_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: changing which tags a monitor shows re-tiles the clients it shows.
 */
void
test_tag_view_change_triggers_tiling(void)
{
  LOG_CLEAN("== Testing a tag view change re-tiles the monitor");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_init();
  tag_manager_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  tag_manager_on_adopt(&m->target);

  Client* tag_one_first  = client_create(2101);
  Client* tag_one_second = client_create(2102);
  Client* tag_two        = client_create(2103);
  assert_or_abort(tag_one_first != NULL && tag_one_second != NULL && tag_two != NULL);

  client_set_tags(tag_one_first, TAG_MASK(0));
  client_set_tags(tag_one_second, TAG_MASK(0));
  client_set_tags(tag_two, TAG_MASK(1));

  client_set_managed(tag_one_first, true);
  client_set_mapped(tag_one_first, true);
  client_set_managed(tag_one_second, true);
  client_set_mapped(tag_one_second, true);
  client_set_managed(tag_two, true);
  client_set_mapped(tag_two, true);

  hub_send_request(REQ_MONITOR_TILE, m->target.id);
  assert(tag_one_second->height == TEST_MONITOR_HEIGHT / 2);

  tag_manager_set_visible_tags(m, TAG_MASK(1));

  assert(tag_one_first->mapped == false);
  assert(tag_one_second->mapped == false);
  assert(tag_two->mapped == true);
  assert(tag_two->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  tag_manager_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: moving the focused client to a hidden tag re-tiles the clients left.
 */
void
test_client_tag_move_triggers_tiling(void)
{
  LOG_CLEAN("== Testing a client moved between tags re-tiles the monitor");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_init();
  focus_component_init();
  tag_manager_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  tag_manager_on_adopt(&m->target);

  Client* first  = client_create(2001);
  Client* second = client_create(2002);
  Client* third  = client_create(2003);
  assert_or_abort(first != NULL && second != NULL && third != NULL);

  client_set_managed(first, true);
  client_set_mapped(first, true);
  client_set_managed(second, true);
  client_set_mapped(second, true);
  client_set_managed(third, true);
  client_set_mapped(third, true);

  hub_send_request(REQ_MONITOR_TILE, m->target.id);

  /* The newest client is master; the two older ones share the stack. */
  assert(third->height == TEST_MONITOR_HEIGHT);
  assert(second->height == TEST_MONITOR_HEIGHT / 2);

  focus_set_state(third, FOCUS_STATE_FOCUSED);
  uint32_t tag = 1;
  hub_send_request_data(REQ_TAG_CLIENT_TOGGLE, third->target.id, &tag);

  assert(third->mapped == false);
  assert(second->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  tag_manager_component_shutdown();
  focus_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

TEST_GROUP(TilingComponent, {
  test_monitor_tile_action_registered();
  test_manage_triggers_tiling();
  test_unmanage_triggers_tiling();
  test_monitor_tile_action_retiles();
  test_monitor_tile_keybinding();
  test_tag_view_change_triggers_tiling();
  test_client_tag_move_triggers_tiling();
});
