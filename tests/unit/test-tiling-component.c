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
#include "src/components/fullscreen.h"
#include "src/components/keybinding.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/core/wm-hub.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"
#include "tests/unit/test-registry.h"
#include "tests/unit/test-wm.h"

#define TEST_MONITOR_OUTPUT 70
#define TEST_MONITOR_WIDTH  800
#define TEST_MONITOR_HEIGHT 600
#define TEST_MASTER_WIDTH   (TEST_MONITOR_WIDTH / 2)

/* The box a window covers on screen, border included. X draws the border
 * inside the window's bounding box, whose origin is the configured x and y, so
 * the inside size plus one border on each side is the whole footprint. */
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
 * Test: a monitor is given its layout state machine when it is registered.
 *
 * Adoption is what tells a component a monitor exists. Tiling used to register
 * after the startup discovery created the monitors, so the startup monitor was
 * never adopted and its layout state only appeared when the first tile request
 * asked for it, while a monitor that arrived later was adopted.
 */
void
test_monitor_gets_layout_sm_on_adoption(void)
{
  LOG_CLEAN("== Testing a monitor adopts tiling when it is registered");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();

  /* Read the stored machine, not tiling_get_sm(): that one creates the SM on
   * demand and would hide a monitor that was never adopted. */
  StateMachine* sm = monitor_get_sm(m, TILING_COMPONENT_NAME);
  assert_or_abort(sm != NULL);
  assert(sm_get_owner(sm) == m);
  assert(tiling_get_state(m) == LAYOUT_STATE_TILE);

  tiling_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
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
    /* Mapped clients carry the border from the map path, and the window is the
     * area minus a border on each side. With nothing in the stack the master
     * area is the whole monitor. */
    assert(c->border_width == 1);
    assert(outer_width(c) == TEST_MONITOR_WIDTH);
    assert(outer_height(c) == TEST_MONITOR_HEIGHT);
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
  assert(outer_height(stack) == TEST_MONITOR_HEIGHT / 2);

  unmap_test_window(1001);

  assert(outer_height(stack) == TEST_MONITOR_HEIGHT);

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
  assert(outer_width(c) == TEST_MONITOR_WIDTH);
  assert(outer_height(c) == TEST_MONITOR_HEIGHT);

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
  assert(outer_width(c) == TEST_MONITOR_WIDTH);
  assert(outer_height(c) == TEST_MONITOR_HEIGHT);

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

  assert(client_is_mapped(tag_one_first) == false);
  assert(client_is_mapped(tag_one_second) == false);
  assert(client_is_mapped(tag_two) == true);
  assert(tag_two->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  tag_manager_component_shutdown();
  client_list_shutdown();
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

  client_set_tags(first, TAG_MASK(0));
  client_set_tags(second, TAG_MASK(0));
  client_set_tags(third, TAG_MASK(0));

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
  uint32_t tag = 2;
  hub_send_request_data(REQ_TAG_CLIENT_MOVE, third->target.id, &tag);

  assert(client_is_mapped(third) == false);
  assert(second->height == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  tag_manager_component_shutdown();
  focus_component_shutdown();
  client_list_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: tiled clients cover their areas edge to edge. A neighbour starts where
 * the previous window's border ends, so the monitor has no gap and no overlap.
 */
void
test_tiled_clients_touch(void)
{
  LOG_CLEAN("== Testing tiled clients touch instead of overlapping by a border");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  map_test_window(1300);
  map_test_window(1301);
  map_test_window(1302);

  /* The list holds the newest client first, and that is the master. */
  Client* master       = client_get_by_window(1302);
  Client* stack_top    = client_get_by_window(1301);
  Client* stack_bottom = client_get_by_window(1300);
  assert_or_abort(master != NULL && stack_top != NULL && stack_bottom != NULL);

  assert(stack_top->x == master->x + (int16_t) outer_width(master));
  assert(outer_width(master) + outer_width(stack_top) == TEST_MONITOR_WIDTH);

  assert(stack_bottom->y == stack_top->y + (int16_t) outer_height(stack_top));
  assert(outer_height(stack_top) + outer_height(stack_bottom) == TEST_MONITOR_HEIGHT);
  assert(outer_height(master) == TEST_MONITOR_HEIGHT);
  assert(master->y == m->y);

  tiling_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: size hints shape the size the layout gives a client
 *
 * A minimum larger than the area wins, because a window drawn smaller than it
 * can draw is the worse outcome. A minimum that fits changes nothing: the
 * window is still the area minus its border, so the tiling stays tight.
 */
void
test_size_hints_shape_the_layout(void)
{
  LOG_CLEAN("== Testing size hints shape the tiled size");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  map_test_window(1400);
  map_test_window(1401);

  Client* master = client_get_by_window(1401);
  Client* stack  = client_get_by_window(1400);
  assert_or_abort(master != NULL && stack != NULL);

  ClientSizeHints hints;
  memset(&hints, 0, sizeof(hints));
  hints.min_width  = 100;
  hints.min_height = 100;
  client_set_size_hints(master, &hints);
  client_set_size_hints(stack, &hints);
  hub_send_request(REQ_MONITOR_TILE, m->target.id);

  assert(master->x + (int16_t) outer_width(master) == stack->x);
  assert(outer_width(master) + outer_width(stack) == TEST_MONITOR_WIDTH);
  assert(outer_height(master) == TEST_MONITOR_HEIGHT);

  /* Wider than the monitor: the hint wins, and the client overruns instead of
   * being drawn too small. */
  memset(&hints, 0, sizeof(hints));
  hints.min_width  = TEST_MONITOR_WIDTH + 200;
  hints.min_height = TEST_MONITOR_HEIGHT + 100;
  client_set_size_hints(master, &hints);
  hub_send_request(REQ_MONITOR_TILE, m->target.id);

  assert(master->width == TEST_MONITOR_WIDTH + 200);
  assert(master->height == TEST_MONITOR_HEIGHT + 100);

  /* A maximum caps the window even when there is more room. */
  memset(&hints, 0, sizeof(hints));
  hints.max_width  = 120;
  hints.max_height = 90;
  client_set_size_hints(master, &hints);
  hub_send_request(REQ_MONITOR_TILE, m->target.id);

  assert(master->width == 120);
  assert(master->height == 90);

  tiling_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: with no master clients the stack has no area of its own, so it shares
 * the master column instead of being drawn at half width.
 *
 * The component's nmaster is 1, so this layout only arrives through a direct
 * call today; the branch is still the layout's, and a hole in the client array
 * must not shift the clients after it.
 */
void
test_arrange_without_a_master_column(void)
{
  LOG_CLEAN("== Testing a stack with no master clients fills the master column");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_init();
  tiling_component_init();

  Monitor* m      = create_test_monitor();
  Client*  first  = client_create(2301);
  Client*  second = client_create(2302);
  assert_or_abort(first != NULL && second != NULL);

  Client* stack[3] = { first, NULL, second };

  tiling_arrange(m, NULL, 0, stack, 3);

  assert(first->x == m->x);
  assert(first->y == m->y);
  assert(outer_width(first) == TEST_MONITOR_WIDTH);
  assert(outer_height(first) == TEST_MONITOR_HEIGHT / 3);
  assert(second->y == first->y + (int16_t) (2 * outer_height(first)));

  /* Nothing to arrange without a monitor. */
  tiling_arrange(NULL, NULL, 0, stack, 3);
  assert(first->y == m->y);

  tiling_component_shutdown();
  client_list_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: a fullscreen client covers its monitor instead of taking a tiled slot,
 * and the clients left behind tile the monitor without it. The state is the
 * client's, so the change reaches tiling as an event and not as a call.
 */
void
test_fullscreen_client_covers_the_monitor(void)
{
  LOG_CLEAN("== Testing a fullscreen client covers the monitor");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  fullscreen_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  map_test_window(1500);
  map_test_window(1501);

  /* The list holds the newest client first, and that is the master. */
  Client* fullscreen = client_get_by_window(1501);
  Client* other      = client_get_by_window(1500);
  assert_or_abort(fullscreen != NULL && other != NULL);
  assert(outer_width(fullscreen) == TEST_MASTER_WIDTH);

  fullscreen_set_state(fullscreen, FULLSCREEN_STATE_FULLSCREEN);

  assert(fullscreen->x == m->x);
  assert(fullscreen->y == m->y);
  assert(outer_width(fullscreen) == TEST_MONITOR_WIDTH);
  assert(outer_height(fullscreen) == TEST_MONITOR_HEIGHT);

  /* The client that is still tiled has the monitor to itself. */
  assert(outer_width(other) == TEST_MONITOR_WIDTH);

  /* A tile request for somebody else leaves the fullscreen window alone. */
  client_set_geometry(fullscreen, 7, 9, 123, 45);
  hub_send_request(REQ_MONITOR_TILE, m->target.id);
  assert(outer_width(fullscreen) == TEST_MONITOR_WIDTH);
  assert(outer_height(fullscreen) == TEST_MONITOR_HEIGHT);

  fullscreen_set_state(fullscreen, FULLSCREEN_STATE_WINDOWED);

  assert(outer_width(fullscreen) == TEST_MASTER_WIDTH);
  assert(outer_width(other) == TEST_MASTER_WIDTH);

  tiling_component_shutdown();
  client_list_component_shutdown();
  fullscreen_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: a monitor whose only visible client is fullscreen has nothing left to
 * tile, and the fullscreen client is still placed. Skipping the client's own
 * placement on that path would leave the window where it was before the key.
 */
void
test_fullscreen_client_alone_on_the_monitor(void)
{
  LOG_CLEAN("== Testing a lone fullscreen client is still placed");

  hub_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  fullscreen_component_init();
  tiling_component_init();

  Monitor* m = create_test_monitor();
  map_test_window(1600);
  map_test_window(1601);

  Client* fullscreen = client_get_by_window(1600);
  assert_or_abort(fullscreen != NULL);

  fullscreen_set_state(fullscreen, FULLSCREEN_STATE_FULLSCREEN);

  /* The window on top goes away, leaving the fullscreen one alone. */
  unmap_test_window(1601);

  /* Something moved the window after that; the next tile has to put it back
   * over the monitor even though the layout has no client to arrange. */
  client_set_geometry(fullscreen, 7, 9, 123, 45);
  hub_send_request(REQ_MONITOR_TILE, m->target.id);

  assert(fullscreen->x == m->x);
  assert(fullscreen->y == m->y);
  assert(outer_width(fullscreen) == TEST_MONITOR_WIDTH);
  assert(outer_height(fullscreen) == TEST_MONITOR_HEIGHT);

  tiling_component_shutdown();
  client_list_component_shutdown();
  fullscreen_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

TEST_GROUP(TilingComponent, {
  test_monitor_tile_action_registered();
  test_monitor_gets_layout_sm_on_adoption();
  test_manage_triggers_tiling();
  test_tiled_clients_touch();
  test_arrange_without_a_master_column();
  test_size_hints_shape_the_layout();
  test_unmanage_triggers_tiling();
  test_monitor_tile_action_retiles();
  test_monitor_tile_keybinding();
  test_tag_view_change_triggers_tiling();
  test_client_tag_move_triggers_tiling();
  test_fullscreen_client_covers_the_monitor();
  test_fullscreen_client_alone_on_the_monitor();
});
