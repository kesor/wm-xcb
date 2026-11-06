/*
 * Test Client Target
 *
 * Tests for the Client target entity including:
 * - Client creation and destruction
 * - Sentinel-based client list management
 * - Client property accessors
 * - Hub registration
 */

#include <string.h>
#include "src/sm/sm-registry.h"
#include "src/sm/sm.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"
#include "wm-log.h"

/* Callback helper for foreach tests */
static int g_callback_count = 0;

static void
inc_count_callback(Client* c)
{
  (void) c;
  g_callback_count++;
}

/*
 * Test client list initialization
 */
void
test_client_list_init(void)
{
  LOG_CLEAN("== Testing client list init");

  hub_init();
  sm_registry_init();
  client_list_init();

  /* Verify empty list */
  assert(client_list_is_empty() == true);
  assert(client_list_get_head() == NULL);
  assert(client_list_count() == 0);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client creation
 */
void
test_client_create(void)
{
  LOG_CLEAN("== Testing client creation");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  if (c == NULL) {
    LOG_ERROR("client_create failed");
    abort();
  }
  assert(c != NULL);
  assert(c->window == 100);
  assert(c->target.type_id == hub_get_target_type_id_by_name("client"));
  assert(c->target.id == 100);
  assert(c->target.registered == true);

  /* Verify registered with Hub */
  HubTarget* t = hub_get_target_by_id(100);
  if (t == NULL) {
    LOG_ERROR("hub_get_target_by_id failed");
    abort();
  }
  assert(t != NULL);
  assert(t->type_id == hub_get_target_type_id_by_name("client"));

  /* Verify in list */
  assert(client_list_get_head() == c);
  assert(client_list_is_empty() == false);
  assert(client_list_count() == 1);

  client_destroy(c);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client creation with XCB_NONE fails
 */
void
test_client_create_none_window(void)
{
  LOG_CLEAN("== Testing client creation with XCB_NONE fails");

  hub_init();
  client_list_init();

  Client* c = client_create(XCB_NONE);
  assert(c == NULL);

  hub_shutdown();
}

/*
 * Test client destruction
 */
void
test_client_destroy(void)
{
  LOG_CLEAN("== Testing client destruction");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  assert(c != NULL);

  /* Verify registered */
  assert(hub_get_target_by_id(100) != NULL);

  client_destroy(c);

  /* Verify unregistered */
  assert(hub_get_target_by_id(100) == NULL);

  /* Verify list is empty */
  assert(client_list_is_empty() == true);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client destroy by window
 */
void
test_client_destroy_by_window(void)
{
  LOG_CLEAN("== Testing client_destroy_by_window");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  assert(c != NULL);
  assert(hub_get_target_by_id(100) != NULL);

  client_destroy_by_window(100);
  assert(hub_get_target_by_id(100) == NULL);

  /* Destroying non-existent should be safe */
  client_destroy_by_window(999);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test sentinel-based client list
 */
void
test_client_sentinel_list(void)
{
  LOG_CLEAN("== Testing sentinel-based client list");

  hub_init();
  sm_registry_init();
  client_list_init();

  /* Create multiple clients */
  Client* c1 = client_create(100);
  Client* c2 = client_create(101);
  Client* c3 = client_create(102);

  assert(c1 != NULL);
  assert(c2 != NULL);
  assert(c3 != NULL);

  /* Verify list order (FIFO - insert at head) */
  assert(client_list_get_head() == c3); /* Last inserted is at head */
  /* Note: tail is sentinel.prev, accessible via iteration if needed */

  /* Verify list count */
  assert(client_list_count() == 3);

  /* Verify contains_window */
  assert(client_list_contains_window(100) == true);
  assert(client_list_contains_window(101) == true);
  assert(client_list_contains_window(102) == true);
  assert(client_list_contains_window(999) == false);

  /* Test next/prev traversal */
  assert(client_get_next(c1) == NULL); /* c1 is tail */
  assert(client_get_next(c2) == c1);
  assert(client_get_next(c3) == c2);

  assert(client_get_prev(c1) == c2);
  assert(client_get_prev(c2) == c3);
  assert(client_get_prev(c3) == NULL); /* c3 is head */

  /* Destroy middle client */
  client_destroy(c2);

  /* Verify list is still linked */
  assert(client_list_count() == 2);
  assert(client_get_next(c3) == c1);
  assert(client_get_prev(c1) == c3);

  /* Clean up */
  client_destroy(c1);
  client_destroy(c3);

  assert(client_list_is_empty() == true);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client foreach iteration
 */
void
test_client_foreach(void)
{
  LOG_CLEAN("== Testing client foreach iteration");

  hub_init();
  sm_registry_init();
  client_list_init();

  /* Create clients */
  client_create(100);
  client_create(101);
  client_create(102);

  /* Count via foreach */
  g_callback_count = 0;
  client_foreach(inc_count_callback);
  assert(g_callback_count == 3);

  /* Count via foreach_reverse */
  g_callback_count = 0;
  client_foreach_reverse(inc_count_callback);
  assert(g_callback_count == 3);

  /* Clean up */
  Client* c = client_list_get_head();
  while (c != NULL) {
    Client* next = client_get_next(c);
    client_destroy(c);
    c = next;
  }

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client property setters/getters
 */
void
test_client_properties(void)
{
  LOG_CLEAN("== Testing client property accessors");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(100);
  assert_or_abort(c != NULL);

  /* Test title */
  char* title = strdup("Test Window");
  assert_or_abort(c != NULL);
  client_set_title(c, title);
  assert_or_abort(c->title != NULL);
  assert(strcmp(c->title, "Test Window") == 0);

  /* Test class */
  char* class_name = strdup("test-class");
  assert_or_abort(c != NULL);
  client_set_class(c, class_name);
  assert_or_abort(c->class_name != NULL);
  // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
  assert(strcmp(c->class_name, "test-class") == 0);

  /* Test geometry */
  client_set_geometry(c, 10, 20, 800, 600);
  assert(c->x == 10);
  assert(c->y == 20);
  assert(c->width == 800);
  assert(c->height == 600);

  /* Test border width */
  client_set_border_width(c, 2);
  assert(c->border_width == 2);

  /* Test tags */
  client_set_tags(c, 0xFF);
  assert(c->tags == 0xFF);

  client_add_tag(c, 5);
  assert((c->tags & (1 << 5)) != 0);

  client_remove_tag(c, 5);
  assert((c->tags & (1 << 5)) == 0);

  assert(client_has_tag(c, 0) == true);
  assert(client_has_tag(c, 7) == true);
  assert(client_has_tag(c, 8) == false);

  /* Test state flags. A new client is in neither exclusive state, and each
   * one is read and written only through its accessor. */
  assert(client_is_urgent(c) == false);
  client_set_urgent(c, true);
  assert(client_is_urgent(c) == true);

  assert(client_is_managed(c) == false);
  client_set_managed(c, true);
  assert(client_is_managed(c) == true);

  client_set_focusable(c, false);
  assert(c->focusable == false);
  assert(client_is_focusable(c) == false);

  /* Test mapped state */
  assert(client_is_mapped(c) == false);
  client_set_mapped(c, true);
  assert(client_is_mapped(c) == true);
  client_set_mapped(c, false);
  assert(client_is_mapped(c) == false);

  /* Test stack mode */
  client_set_stack_mode(c, XCB_STACK_MODE_BELOW);
  assert(client_get_stack_mode(c) == XCB_STACK_MODE_BELOW);
  client_set_stack_mode(c, XCB_STACK_MODE_ABOVE);
  assert(client_get_stack_mode(c) == XCB_STACK_MODE_ABOVE);

  client_destroy(c);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client lookup by window
 */
void
test_client_get_by_window(void)
{
  LOG_CLEAN("== Testing client lookup by window");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c1 = client_create(100);
  Client* c2 = client_create(101);

  /* Find existing via Hub */
  Client* found = client_get_by_window(100);
  assert(found == c1);

  found = client_get_by_window(101);
  assert(found == c2);

  /* Find non-existent */
  found = client_get_by_window(999);
  assert(found == NULL);

  /* Clean up */
  client_destroy(c1);
  client_destroy(c2);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client count managed
 */
void
test_client_count_managed(void)
{
  LOG_CLEAN("== Testing client count managed");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c1 = client_create(100);
  Client* c2 = client_create(101);
  Client* c3 = client_create(102);

  assert(client_count_managed() == 0);

  client_set_managed(c1, true);
  assert(client_count_managed() == 1);

  client_set_managed(c2, true);
  assert(client_count_managed() == 2);

  client_set_managed(c3, true);
  assert(client_count_managed() == 3);

  client_set_managed(c2, false);
  assert(client_count_managed() == 2);

  /* Clean up */
  client_destroy(c1);
  client_destroy(c2);
  client_destroy(c3);

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test duplicate client creation rejected
 */
void
test_client_duplicate_creation(void)
{
  LOG_CLEAN("== Testing duplicate client creation is rejected");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c1 = client_create(100);
  assert(c1 != NULL);

  /* Try to create another client with same window - should fail */
  Client* c2 = client_create(100);
  assert(c2 == NULL); /* Hub should reject duplicate ID */

  /* Original client should still be valid */
  Client* found = client_get_by_window(100);
  assert(found == c1);

  client_destroy(c1);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test WM_STATE is skipped when no atom was interned
 */
void
test_client_wm_state_without_connection(void)
{
  LOG_CLEAN("== Testing WM_STATE is skipped without an X connection");

  /* Unit tests never connect, so client_atoms_init() interns nothing. The
   * property change must then be skipped: sending it would use a None atom and
   * draw a BadAtom error from the server. */
  client_atoms_init();
  assert(client_set_wm_state(100, CLIENT_WM_STATE_NORMAL) == false);
}

/*
 * Test the WM_NORMAL_HINTS parser
 *
 * WM_NORMAL_HINTS is a fixed 18-slot struct: a flag word and then every field
 * at its own offset, with the ones a flag does not name left at zero. Xlib's
 * XSetWMNormalHints() writes it that way and XGetWMNormalHints() reads it that
 * way. A reader that walks the flags instead takes the minimum from the
 * position slots, which are zero for a client that only named a minimum.
 */
void
test_client_size_hints_parse(void)
{
  LOG_CLEAN("== Testing the WM_NORMAL_HINTS parser");

  ClientSizeHints hints;

  int32_t full[18] = { 0 };
  full[0]          = CLIENT_SIZE_HINT_P_MIN_SIZE | CLIENT_SIZE_HINT_P_MAX_SIZE |
            CLIENT_SIZE_HINT_P_RESIZE_INC | CLIENT_SIZE_HINT_P_ASPECT |
            CLIENT_SIZE_HINT_P_BASE_SIZE | CLIENT_SIZE_HINT_P_WIN_GRAVITY;
  full[CLIENT_SIZE_HINT_MIN_WIDTH]   = 100;
  full[CLIENT_SIZE_HINT_MIN_HEIGHT]  = 50;
  full[CLIENT_SIZE_HINT_MAX_WIDTH]   = 800;
  full[CLIENT_SIZE_HINT_MAX_HEIGHT]  = 600;
  full[CLIENT_SIZE_HINT_WIDTH_INC]   = 8;
  full[CLIENT_SIZE_HINT_HEIGHT_INC]  = 6;
  full[CLIENT_SIZE_HINT_ASPECT_MIN]  = 1;
  full[CLIENT_SIZE_HINT_BASE_WIDTH]  = 20;
  full[CLIENT_SIZE_HINT_BASE_HEIGHT] = 10;
  full[CLIENT_SIZE_HINT_GRAVITY]     = 1;

  assert(client_size_hints_from_property(full, 18, &hints) == true);
  assert(hints.min_width == 100);
  assert(hints.min_height == 50);
  assert(hints.max_width == 800);
  assert(hints.max_height == 600);
  assert(hints.inc_width == 8);
  assert(hints.inc_height == 6);
  assert(hints.base_width == 20);
  assert(hints.base_height == 10);

  /* The case Xlib writes for a client that sets only PMinSize: slots 1 to 4
   * are present and zero because the position and size flags are clear. */
  int32_t min_only[18]                  = { 0 };
  min_only[0]                           = CLIENT_SIZE_HINT_P_MIN_SIZE;
  min_only[CLIENT_SIZE_HINT_MIN_WIDTH]  = 320;
  min_only[CLIENT_SIZE_HINT_MIN_HEIGHT] = 200;
  assert(client_size_hints_from_property(min_only, 18, &hints) == true);
  assert(hints.min_width == 320);
  assert(hints.min_height == 200);
  assert(hints.max_width == 0);
  assert(hints.base_width == 0);
  assert(hints.inc_width == 0);

  /* Pre-ICCCM clients write 15 slots, which stop before base size and
   * gravity. The minimum and the increment still read from their offsets. */
  int32_t old[15]                  = { 0 };
  old[0]                           = CLIENT_SIZE_HINT_P_MIN_SIZE | CLIENT_SIZE_HINT_P_RESIZE_INC;
  old[CLIENT_SIZE_HINT_MIN_WIDTH]  = 64;
  old[CLIENT_SIZE_HINT_MIN_HEIGHT] = 48;
  old[CLIENT_SIZE_HINT_WIDTH_INC]  = 4;
  old[CLIENT_SIZE_HINT_HEIGHT_INC] = 3;
  assert(client_size_hints_from_property(old, 15, &hints) == true);
  assert(hints.min_width == 64);
  assert(hints.min_height == 48);
  assert(hints.inc_width == 4);
  assert(hints.inc_height == 3);
  assert(hints.base_width == 0);

  /* The values are INT32. Read as unsigned, -100 would become 65535 and pin a
   * window to the largest size there is. */
  int32_t negative[18]                  = { 0 };
  negative[0]                           = CLIENT_SIZE_HINT_P_MIN_SIZE;
  negative[CLIENT_SIZE_HINT_MIN_WIDTH]  = -100;
  negative[CLIENT_SIZE_HINT_MIN_HEIGHT] = 40;
  assert(client_size_hints_from_property(negative, 18, &hints) == true);
  assert(hints.min_width == 0);
  assert(hints.min_height == 40);

  /* A property that is not a WM_SIZE_HINTS is rejected: nothing is taken from
   * it, and the caller keeps the constraints it had. */
  ClientSizeHints untouched;
  memset(&untouched, 7, sizeof(untouched));
  ClientSizeHints before = untouched;

  int32_t too_short[14] = { 0 };
  too_short[0]          = CLIENT_SIZE_HINT_P_MIN_SIZE;
  assert(client_size_hints_from_property(too_short, 14, &untouched) == false);
  assert(memcmp(&untouched, &before, sizeof(untouched)) == 0);

  int32_t old_with_base[15] = { 0 };
  old_with_base[0]          = CLIENT_SIZE_HINT_P_MIN_SIZE | CLIENT_SIZE_HINT_P_BASE_SIZE;
  assert(client_size_hints_from_property(old_with_base, 15, &untouched) == false);
  assert(memcmp(&untouched, &before, sizeof(untouched)) == 0);

  /* No property at all: the client is unconstrained. */
  assert(client_size_hints_from_property(NULL, 0, &hints) == false);
}

/*
 * Test applying size hints to a size
 *
 * The layout has to be able to hand a client its area and get back a size the
 * client can draw in. Minimum wins over the area, maximum caps it, and the
 * increment counts from the base size.
 */
void
test_client_apply_size_hints(void)
{
  LOG_CLEAN("== Testing size hints narrow a layout size");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(500);
  assert_or_abort(c != NULL);

  uint16_t w = 400;
  uint16_t h = 300;
  client_apply_size_hints(c, &w, &h);
  assert(w == 400);
  assert(h == 300);

  ClientSizeHints hints;
  memset(&hints, 0, sizeof(hints));
  hints.min_width  = 500;
  hints.min_height = 260;
  client_set_size_hints(c, &hints);

  w = 400;
  h = 300;
  client_apply_size_hints(c, &w, &h);
  assert(w == 500);
  assert(h == 300);

  memset(&hints, 0, sizeof(hints));
  hints.max_width  = 200;
  hints.max_height = 100;
  client_set_size_hints(c, &hints);

  w = 400;
  h = 300;
  client_apply_size_hints(c, &w, &h);
  assert(w == 200);
  assert(h == 100);

  /* 400 is 20 above a base of 30, and 20 is not a multiple of 16: the step
   * rounds down, so the window stays inside the area it was given. */
  memset(&hints, 0, sizeof(hints));
  hints.base_width  = 30;
  hints.base_height = 10;
  hints.inc_width   = 16;
  hints.inc_height  = 7;
  client_set_size_hints(c, &hints);

  w = 400;
  h = 300;
  client_apply_size_hints(c, &w, &h);
  assert(w == 30 + 16 * 23);
  assert(h == 10 + 7 * 41);

  /* A client that sets PMinSize and PResizeInc and no base means the minimum
   * as its base (ICCCM 4.1.2.3), so the size it gets is min + k * inc and the
   * minimum does not overwrite a size off that grid. */
  memset(&hints, 0, sizeof(hints));
  hints.min_width  = 40;
  hints.min_height = 30;
  hints.inc_width  = 16;
  hints.inc_height = 7;
  client_set_size_hints(c, &hints);

  w = 400;
  h = 300;
  client_apply_size_hints(c, &w, &h);
  assert(w == 40 + 16 * 22);
  assert(h == 30 + 7 * 38);

  /* A client that sets only a base size gets that size as its minimum. */
  memset(&hints, 0, sizeof(hints));
  hints.base_width  = 60;
  hints.base_height = 20;
  client_set_size_hints(c, &hints);

  w = 10;
  h = 10;
  client_apply_size_hints(c, &w, &h);
  assert(w == 60);
  assert(h == 20);

  /* A size never collapses to nothing, whatever the hints say. */
  memset(&hints, 0, sizeof(hints));
  client_set_size_hints(c, &hints);
  w = 0;
  h = 0;
  client_apply_size_hints(c, &w, &h);
  assert(w == 1);
  assert(h == 1);

  client_destroy(c);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test the size hint read is skipped without an X connection
 *
 * The read is a request whose reply arrives later. With no connection there is
 * nothing to send and nothing to read, and the client keeps the constraints it
 * has, which are none.
 */
void
test_client_size_hints_without_connection(void)
{
  LOG_CLEAN("== Testing the size hint read is skipped without an X connection");

  hub_init();
  sm_registry_init();
  client_list_init();

  Client* c = client_create(501);
  assert_or_abort(c != NULL);

  client_request_size_hints(c);
  assert(c->size_hints_pending == false);
  assert(client_poll_size_hints(c) == false);

  uint16_t w = 400;
  uint16_t h = 300;
  client_apply_size_hints(c, &w, &h);
  assert(w == 400);
  assert(h == 300);

  client_destroy(c);
  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client with monitor association
 *
 * A new client belongs to the selected monitor and carries that monitor's
 * visible tags, because tiling and tag membership both read those fields.
 */
void
test_client_monitor_association(void)
{
  LOG_CLEAN("== Testing client monitor association");

  hub_init();
  sm_registry_init();
  monitor_list_init();
  client_list_init();

  Monitor* m = monitor_create(200);
  monitor_set_geometry(m, 0, 0, 800, 600);
  monitor_set_tagset(m, 1U << 2); /* tag 3 */

  Client* c = client_create(100);

  assert(client_get_monitor(c) == m);
  assert(client_has_tag(c, 2));
  assert(c->tags == m->tagset);

  client_destroy(c);
  monitor_destroy(m);
  monitor_list_shutdown();

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Test client created when the monitor list is empty.
 *
 * RandR discovery runs before the event loop (monitor_manager_init in wm.c),
 * so a window that appears during the session already has a monitor. This is
 * the empty-list case: client_create must still return a usable client.
 */
void
test_client_without_monitor(void)
{
  LOG_CLEAN("== Testing client created when the monitor list is empty");

  hub_init();
  sm_registry_init();
  monitor_list_init();
  client_list_init();

  assert(monitor_get_selected() == NULL);

  Client* c = client_create(100);
  assert(client_get_monitor(c) == NULL);
  assert(c->tags == 0);

  client_destroy(c);
  monitor_list_shutdown();

  hub_shutdown();
  sm_registry_shutdown();
}

/*
 * Selected monitor wins over list order. With nothing selected, the client
 * takes the first monitor in the list. A one-monitor test cannot tell those
 * apart: monitor_create() selects the monitor it prepends, which is also the
 * list head.
 */
void
test_client_monitor_selection_and_fallback(void)
{
  LOG_CLEAN("== Testing client monitor follows selection, then list head");

  hub_init();
  sm_registry_init();
  monitor_list_init();
  client_list_init();

  Monitor* older = monitor_create(301);
  Monitor* newer = monitor_create(302);
  monitor_set_tagset(older, MONITOR_TAG_MASK(1));
  monitor_set_tagset(newer, MONITOR_TAG_MASK(3));

  assert(monitor_list_get_first() == newer);
  monitor_set_selected(older);

  Client* on_selected = client_create(401);
  assert(client_get_monitor(on_selected) == older);
  assert(on_selected->tags == older->tagset);
  assert(on_selected->tags != newer->tagset);

  monitor_set_selected(NULL);

  Client* on_first = client_create(402);
  assert(client_get_monitor(on_first) == newer);
  assert(on_first->tags == newer->tagset);

  client_destroy(on_selected);
  client_destroy(on_first);
  monitor_destroy(older);
  monitor_destroy(newer);
  monitor_list_shutdown();

  hub_shutdown();
  sm_registry_shutdown();
}

TEST_GROUP(ClientTarget, {
  test_client_list_init();
  test_client_create();
  test_client_create_none_window();
  test_client_destroy();
  test_client_destroy_by_window();
  test_client_sentinel_list();
  test_client_foreach();
  test_client_properties();
  test_client_get_by_window();
  test_client_count_managed();
  test_client_duplicate_creation();
  test_client_wm_state_without_connection();
  test_client_size_hints_parse();
  test_client_apply_size_hints();
  test_client_size_hints_without_connection();
  test_client_without_monitor();
  test_client_monitor_association();
  test_client_monitor_selection_and_fallback();
});
