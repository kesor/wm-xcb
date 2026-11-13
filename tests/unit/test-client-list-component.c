/*
 * Client List Component Tests
 *
 * Tests for the client-list component that handles XCB events
 * for client lifecycle management.
 */

#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/floating.h"
#include "src/components/focus.h"
#include "src/components/tiling.h"
#include "src/core/wm-hub.h"
#include "src/sm/sm-registry.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "tests/unit/test-registry.h"
#include "tests/unit/test-wm.h"

/* Counts EVT_CLIENT_UNMANAGED so a test can tell the WM hiding a window from a
 * client taking its window away. */
static uint32_t unmanaged_events;

static void
unmanaged_listener(Event e)
{
  (void) e;
  unmanaged_events++;
}

/*
 * Test component initialization
 */
void
test_client_list_component_init(void)
{
  LOG_CLEAN("== Testing client list component init");

  hub_init();
  xcb_handler_init();

  /* Initially not initialized */
  assert(client_list_component_is_initialized() == false);

  /* Initialize should succeed */
  bool result = client_list_component_init();
  assert(result == true);
  assert(client_list_component_is_initialized() == true);

  /* Re-init should be safe */
  result = client_list_component_init();
  assert(result == true);

  /* Should have registered XCB handlers */
  assert(xcb_handler_count_for_type(XCB_CREATE_NOTIFY) >= 1);
  assert(xcb_handler_count_for_type(XCB_DESTROY_NOTIFY) >= 1);
  assert(xcb_handler_count_for_type(XCB_MAP_REQUEST) >= 1);
  assert(xcb_handler_count_for_type(XCB_UNMAP_NOTIFY) >= 1);
  assert(xcb_handler_count_for_type(XCB_PROPERTY_NOTIFY) >= 1);

  /* Should have registered with hub */
  HubComponent* comp = hub_get_component_by_name(CLIENT_LIST_COMPONENT_NAME);
  assert(comp != NULL);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test component shutdown
 */
void
test_client_list_component_shutdown(void)
{
  LOG_CLEAN("== Testing client list component shutdown");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Verify handlers are registered */
  assert(xcb_handler_count_for_type(XCB_CREATE_NOTIFY) >= 1);

  client_list_component_shutdown();

  /* Handlers should be unregistered */
  assert(xcb_handler_count_for_type(XCB_CREATE_NOTIFY) == 0);
  assert(xcb_handler_count_for_type(XCB_DESTROY_NOTIFY) == 0);
  assert(xcb_handler_count_for_type(XCB_MAP_REQUEST) == 0);
  assert(xcb_handler_count_for_type(XCB_UNMAP_NOTIFY) == 0);
  assert(xcb_handler_count_for_type(XCB_PROPERTY_NOTIFY) == 0);

  /* Should be uninitialized */
  assert(client_list_component_is_initialized() == false);

  /* Component should be unregistered from hub */
  HubComponent* comp = hub_get_component_by_name(CLIENT_LIST_COMPONENT_NAME);
  assert(comp == NULL);

  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test CREATE_NOTIFY handler creates client
 */
void
test_create_notify_creates_client(void)
{
  LOG_CLEAN("== Testing CREATE_NOTIFY creates client");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Initially no clients */
  assert(client_list_count() == 0);

  /* Create a fake CREATE_NOTIFY event */
  xcb_create_notify_event_t event = {
    .response_type     = 16,  /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100, /* root */
    .window            = 200, /* new window */
    .x                 = 10,
    .y                 = 20,
    .width             = 400,
    .height            = 300,
    .border_width      = 2,
    .override_redirect = 0,
  };

  /* Dispatch event */
  xcb_handler_dispatch(&event);

  /* Client should be created */
  assert(client_list_count() == 1);
  Client* c = client_get_by_window(200);
  assert(c != NULL);
  assert(c != NULL && c->window == 200);

  /* Geometry should be set from event */
  assert(c != NULL && c->x == 10);
  assert(c != NULL && c->y == 20);
  assert(c != NULL && c->width == 400);
  assert(c != NULL && c->height == 300);
  assert(c != NULL && c->border_width == 2);

  /* Client should be registered with hub */
  HubTarget* t = hub_get_target_by_id(200);
  assert(t != NULL);
  assert(t != NULL && t->type_id == hub_get_target_type_id_by_name("client"));

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test CREATE_NOTIFY skips override-redirect windows
 */
void
test_create_notify_skips_override_redirect(void)
{
  LOG_CLEAN("== Testing CREATE_NOTIFY skips override-redirect windows");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Create override-redirect event (like a popup) */
  xcb_create_notify_event_t event = {
    .response_type     = 16,  /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = 300, /* popup window */
    .x                 = 50,
    .y                 = 50,
    .width             = 200,
    .height            = 100,
    .border_width      = 0,
    .override_redirect = 1,
  };

  xcb_handler_dispatch(&event);

  /* Should NOT create client */
  assert(client_list_count() == 0);
  assert(client_get_by_window(300) == NULL);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test DESTROY_NOTIFY destroys client
 */
void
test_destroy_notify_destroys_client(void)
{
  LOG_CLEAN("== Testing DESTROY_NOTIFY destroys client");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* First create a client via CREATE_NOTIFY */
  xcb_create_notify_event_t create_event = {
    .response_type     = 16, /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = 400,
    .x                 = 0,
    .y                 = 0,
    .width             = 100,
    .height            = 100,
    .border_width      = 0,
    .override_redirect = 0,
  };
  xcb_handler_dispatch(&create_event);

  assert(client_list_count() == 1);
  Client* c = client_get_by_window(400);
  assert(c != NULL);

  /* Now send DESTROY_NOTIFY */
  xcb_destroy_notify_event_t destroy_event = {
    .response_type = 17, /* XCB_DESTROY_NOTIFY */
    .sequence      = 2,
    .event         = 400,
    .window        = 400,
  };
  xcb_handler_dispatch(&destroy_event);

  /* Client should be destroyed */
  assert(client_list_count() == 0);
  assert(client_get_by_window(400) == NULL);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test MAP_REQUEST manages client
 */
void
test_map_request_manages_client(void)
{
  LOG_CLEAN("== Testing MAP_REQUEST manages client");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* First create client via CREATE_NOTIFY */
  xcb_create_notify_event_t create_event = {
    .response_type     = 16, /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = 500,
    .x                 = 0,
    .y                 = 0,
    .width             = 100,
    .height            = 100,
    .border_width      = 0,
    .override_redirect = 0,
  };
  xcb_handler_dispatch(&create_event);

  Client* c = client_get_by_window(500);
  assert(c != NULL);
  assert(c != NULL && client_is_managed(c) == false);

  /* Send MAP_REQUEST */
  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 2,
    .parent        = 100,
    .window        = 500,
  };
  xcb_handler_dispatch(&map_event);

  /* Client should now be managed and shown */
  assert(c != NULL && client_is_managed(c) == true);
  assert(c != NULL && client_is_mapped(c) == true);

  /* A repeat MapRequest is not a second manage: the window is already shown */
  map_event.sequence = 3;
  xcb_handler_dispatch(&map_event);
  assert(client_list_managed_count() == 1);
  assert(client_list_count() == 1);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * The event mask a managed window selects.
 *
 * A window sends no PropertyNotify unless someone selected PropertyChange on
 * it, so a missing bit is a handler that never runs - and xwininfo reports the
 * union of every client's selection, which is why a seam test cannot see this
 * without a client that asks for nothing. The bits are pinned here so that an
 * edit to the mask fails in the unit suite as well.
 */
void
test_managed_event_mask_covers_the_readers(void)
{
  LOG_CLEAN("== Testing the event mask a managed window selects");

  uint32_t mask = client_list_managed_event_mask();

  assert((mask & XCB_EVENT_MASK_ENTER_WINDOW) != 0);
  assert((mask & XCB_EVENT_MASK_LEAVE_WINDOW) != 0);
  assert((mask & XCB_EVENT_MASK_PROPERTY_CHANGE) != 0);
}

/*
 * Test MAP_REQUEST sets the border width in the model
 *
 * The unit suite runs without an X connection, so client_list_prepare_client()
 * sends no request; the width it asks X for still has to reach the client.
 */
void
test_map_request_sets_border_width(void)
{
  LOG_CLEAN("== Testing MAP_REQUEST sets the client border width");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 1,
    .parent        = 100,
    .window        = 700,
  };
  xcb_handler_dispatch(&map_event);

  Client* c = client_get_by_window(700);
  assert(c != NULL);
  assert(c != NULL && c->border_width == 1);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test MAP_REQUEST creates client if not exists
 */
void
test_map_request_creates_client_if_not_exists(void)
{
  LOG_CLEAN("== Testing MAP_REQUEST creates client if not exists");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  assert(client_list_count() == 0);

  /* Send MAP_REQUEST without prior CREATE_NOTIFY */
  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 1,
    .parent        = 100,
    .window        = 600,
  };
  xcb_handler_dispatch(&map_event);

  /* Client should be created and managed */
  assert(client_list_count() == 1);
  Client* c = client_get_by_window(600);
  assert(c != NULL);
  assert(c != NULL && client_is_managed(c) == true);
  assert(c != NULL && client_is_mapped(c) == true);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test UNMAP_NOTIFY unmanages client
 */
void
test_unmap_notify_unmanages_client(void)
{
  LOG_CLEAN("== Testing UNMAP_NOTIFY unmanages client");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Create and manage client */
  xcb_create_notify_event_t create_event = {
    .response_type     = 16, /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = 700,
    .x                 = 0,
    .y                 = 0,
    .width             = 100,
    .height            = 100,
    .border_width      = 0,
    .override_redirect = 0,
  };
  xcb_handler_dispatch(&create_event);

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 2,
    .parent        = 100,
    .window        = 700,
  };
  xcb_handler_dispatch(&map_event);

  Client* c = client_get_by_window(700);
  assert(c != NULL && client_is_managed(c) == true);
  assert(c != NULL && client_is_mapped(c) == true);

  /* Send UNMAP_NOTIFY */
  xcb_unmap_notify_event_t unmap_event = {
    .response_type  = 18, /* XCB_UNMAP_NOTIFY */
    .sequence       = 3,
    .event          = 700,
    .window         = 700,
    .from_configure = 0,
  };

  unmanaged_events = 0;
  hub_subscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener, NULL);
  xcb_handler_dispatch(&unmap_event);
  hub_unsubscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener);

  /* A withdrawal is announced: the focus component moves focus away on it */
  assert(unmanaged_events == 1);

  /* Client should no longer be managed or shown */
  assert(c != NULL && client_is_managed(c) == false);
  assert(c != NULL && client_is_mapped(c) == false);

  /* Client still exists (just unmanaged) */
  assert(client_list_count() == 1);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Hand a window to the WM the way the server does: create, then ask to map.
 * Returns the client the component made for it.
 */
static Client*
manage_test_window(xcb_window_t window)
{
  xcb_create_notify_event_t create_event = {
    .response_type     = 16, /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = window,
    .width             = 100,
    .height            = 100,
    .override_redirect = 0,
  };
  xcb_handler_dispatch(&create_event);

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 2,
    .parent        = 100,
    .window        = window,
  };
  xcb_handler_dispatch(&map_event);

  return client_get_by_window(window);
}

/*
 * Test UNMAP_NOTIFY for a window the WM unmapped itself keeps the client
 *
 * Hiding the clients of a tag that is no longer shown unmaps them. That unmap
 * is not a withdrawal: clearing management here makes the client disappear from
 * the WM, and showing the tag again has nothing to map back.
 */
void
test_unmap_notify_of_a_wm_unmap_keeps_the_client(void)
{
  LOG_CLEAN("== Testing UNMAP_NOTIFY of a WM unmap keeps the client");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  Client* c = manage_test_window(800);
  assert(c != NULL && client_is_managed(c) == true);
  assert(c != NULL && client_is_mapped(c) == true);

  /* What the tag manager does for a client on a tag that is not shown */
  client_hide(800);
  client_set_mapped(c, false);

  xcb_unmap_notify_event_t unmap_event = {
    .response_type  = 18, /* XCB_UNMAP_NOTIFY */
    .sequence       = 3,
    .event          = 800,
    .window         = 800,
    .from_configure = 0,
  };

  unmanaged_events = 0;
  hub_subscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener, NULL);
  xcb_handler_dispatch(&unmap_event);
  hub_unsubscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener);

  assert(unmanaged_events == 0);
  assert(c != NULL && client_is_managed(c) == true);
  assert(c != NULL && client_is_mapped(c) == false);
  assert(client_list_managed_count() == 1);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test a hide answered after a later show keeps the client managed
 *
 * The loop drains every queued event before it flushes, so a burst of tag
 * actions runs a tag's hide and a second tag's show before either UnmapNotify
 * is read. `mapped` is true again by then, and the notify still belongs to the
 * hide. Only the count of owed notifies can tell.
 */
void
test_unmap_notify_of_a_hide_after_a_show_keeps_the_client(void)
{
  LOG_CLEAN("== Testing UNMAP_NOTIFY of a hide followed by a show");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  Client* c = manage_test_window(900);
  assert(c != NULL && client_is_managed(c) == true);

  /* Two tag actions in one drain: hide for the old tag, show for the new */
  client_hide(900);
  client_set_mapped(c, false);
  client_show(900);
  client_set_mapped(c, true);

  xcb_unmap_notify_event_t unmap_event = {
    .response_type  = 18, /* XCB_UNMAP_NOTIFY */
    .sequence       = 3,
    .event          = 900,
    .window         = 900,
    .from_configure = 0,
  };

  unmanaged_events = 0;
  hub_subscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener, NULL);
  xcb_handler_dispatch(&unmap_event);
  hub_unsubscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener);

  assert(unmanaged_events == 0);
  assert(c != NULL && client_is_managed(c) == true);
  assert(c != NULL && client_is_mapped(c) == true);
  assert(client_list_managed_count() == 1);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test a hidden client that withdraws is still unmanaged
 *
 * The server sends no UnmapNotify for a window it already has unmapped, so
 * ICCCM 4.1.3.1 has a client withdrawing an unmapped window send a synthetic
 * notify. That notify is not an answer to a hide the WM asked for, and the
 * client is unmanaged on it.
 */
void
test_synthetic_unmap_notify_of_a_hidden_client_unmanages(void)
{
  LOG_CLEAN("== Testing a synthetic UNMAP_NOTIFY unmanages a hidden client");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  Client* c = manage_test_window(950);
  assert(c != NULL && client_is_managed(c) == true);

  client_hide(950);
  client_set_mapped(c, false);

  xcb_unmap_notify_event_t hide_event = {
    .response_type  = 18, /* XCB_UNMAP_NOTIFY */
    .sequence       = 3,
    .event          = 950,
    .window         = 950,
    .from_configure = 0,
  };
  xcb_handler_dispatch(&hide_event);
  assert(c != NULL && client_is_managed(c) == true);

  /* The client withdraws the window it cannot unmap again */
  xcb_unmap_notify_event_t withdraw_event = {
    .response_type  = 18 | 0x80, /* synthetic, sent by the client */
    .sequence       = 4,
    .event          = 950,
    .window         = 950,
    .from_configure = 0,
  };

  unmanaged_events = 0;
  hub_subscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener, NULL);
  xcb_handler_dispatch(&withdraw_event);
  hub_unsubscribe(EVT_CLIENT_UNMANAGED, unmanaged_listener);

  assert(unmanaged_events == 1);
  assert(c != NULL && client_is_managed(c) == false);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test a managed client lands on a monitor and is tiled there
 *
 * A client with no monitor never reaches tiling_collect_clients(), so the
 * whole window stays where the client put it.
 */
void
test_managed_client_lands_on_monitor(void)
{
  LOG_CLEAN("== Testing a managed client lands on the selected monitor");

  hub_init();
  xcb_handler_init();
  monitor_list_init();
  client_list_component_init();

  Monitor* m = monitor_create(77);
  monitor_set_geometry(m, 0, 0, 800, 600);

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 1,
    .parent        = 100,
    .window        = 800,
  };
  xcb_handler_dispatch(&map_event);

  Client* c = client_get_by_window(800);
  assert(c != NULL);
  assert(client_get_monitor(c) == m);

  /* Tile: the only client is master and there is no stack, so it gets the whole
   * monitor. The window is that area minus one border on each side, because X
   * draws the border inside the window's bounding box. */
  tiling_tile_monitor(m);
  uint16_t border = c->border_width;
  assert(border > 0);
  assert(c->width == m->width - 2 * border);
  assert(c->height == m->height - 2 * border);

  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
  monitor_list_shutdown();
}

/*
 * Test multiple clients in list
 */
void
test_multiple_clients(void)
{
  LOG_CLEAN("== Testing multiple clients in list");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Create multiple clients */
  for (uint32_t i = 1; i <= 5; i++) {
    xcb_create_notify_event_t event = {
      .response_type     = 16, /* XCB_CREATE_NOTIFY */
      .sequence          = i,
      .parent            = 100,
      .window            = 1000 + i,
      .x                 = (int16_t) (i * 10),
      .y                 = (int16_t) (i * 10),
      .width             = 100,
      .height            = 100,
      .border_width      = 0,
      .override_redirect = 0,
    };
    xcb_handler_dispatch(&event);
  }

  assert(client_list_count() == 5);

  /* Verify each client exists */
  for (uint32_t i = 1; i <= 5; i++) {
    Client* c = client_get_by_window(1000 + i);
    assert(c != NULL);
    assert(c != NULL && c->x == (i * 10));
  }

  /* Destroy middle client */
  xcb_destroy_notify_event_t destroy_event = {
    .response_type = 17, /* XCB_DESTROY_NOTIFY */
    .sequence      = 10,
    .event         = 1002,
    .window        = 1002,
  };
  xcb_handler_dispatch(&destroy_event);

  assert(client_list_count() == 4);

  /* Other clients should still exist */
  assert(client_get_by_window(1001) != NULL);
  assert(client_get_by_window(1002) == NULL); /* destroyed */
  assert(client_get_by_window(1003) != NULL);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test utility functions
 */
void
test_client_list_utility_functions(void)
{
  LOG_CLEAN("== Testing client list utility functions");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Initially empty */
  assert(client_list_managed_count() == 0);
  assert(client_list_count() == 0);
  assert(client_list_is_empty() == true);

  /* Create and manage client */
  xcb_create_notify_event_t create = {
    .response_type     = 16, /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = 3000,
    .override_redirect = 0,
  };
  xcb_handler_dispatch(&create);

  xcb_map_request_event_t map = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 2,
    .parent        = 100,
    .window        = 3000,
  };
  xcb_handler_dispatch(&map);

  /* Check state */
  assert(client_list_count() == 1);
  assert(client_list_managed_count() == 1);
  assert(client_list_is_empty() == false);
  assert(client_list_is_managed(3000) == true);
  assert(client_list_contains_window(3000) == true);

  /* Unmanage */
  xcb_unmap_notify_event_t unmap = {
    .response_type  = 18, /* XCB_UNMAP_NOTIFY */
    .sequence       = 3,
    .event          = 3000,
    .window         = 3000,
    .from_configure = 0,
  };
  xcb_handler_dispatch(&unmap);

  assert(client_list_managed_count() == 0);
  assert(client_list_is_managed(3000) == false);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test client adopts components on creation
 */
void
test_client_adopts_components_on_creation(void)
{
  LOG_CLEAN("== Testing client adopts components on creation");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Create a client */
  xcb_create_notify_event_t event = {
    .response_type     = 16, /* XCB_CREATE_NOTIFY */
    .sequence          = 1,
    .parent            = 100,
    .window            = 4000,
    .override_redirect = 0,
  };
  xcb_handler_dispatch(&event);

  Client* c = client_get_by_window(4000);
  assert(c != NULL);

  /* Verify client is registered as TARGET_TYPE_CLIENT */
  HubTarget* t = hub_get_target_by_id(4000);
  assert(t != NULL);
  assert(t != NULL && t->type_id == hub_get_target_type_id_by_name("client"));

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * The property a client rewrites when its size constraints change.
 *
 * A PropertyNotify for any other property has to leave the client alone: the
 * WM reads _NET_WM_STATE and the hints off the same event type, and a handler
 * that answered everything would re-read the hints on every one of them.
 * Without an X connection the atom was never interned, so the second half -
 * that WM_NORMAL_HINTS is recognised and re-read - is the seam test
 * test-31-size-hints-changed.sh, which needs the server that owns the atom.
 */
void
test_property_notify_ignores_other_properties(void)
{
  LOG_CLEAN("== Testing PROPERTY_NOTIFY ignores a property that is not WM_NORMAL_HINTS");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  xcb_create_notify_event_t create_event = {
    .response_type = 16, /* XCB_CREATE_NOTIFY */
    .window        = 800,
  };
  xcb_handler_dispatch(&create_event);

  Client* c = client_get_by_window(800);
  assert(c != NULL);
  client_set_managed(c, true);

  xcb_property_notify_event_t title_event = {
    .response_type = 28, /* XCB_PROPERTY_NOTIFY */
    .window        = 800,
    .atom          = 39, /* any atom: no connection interned the real ones */
    .state         = XCB_PROPERTY_NEW_VALUE,
  };
  xcb_handler_dispatch(&title_event);

  assert(c->size_hints_pending == false);
  assert(c->size_hints.min_width == 0);

  /* A change to a window the WM never managed is not the WM's to lay out. */
  title_event.window = 801;
  xcb_handler_dispatch(&title_event);

  assert(client_get_by_window(801) == NULL);

  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test shutdown with clients still in list
 */
void
test_shutdown_with_clients(void)
{
  LOG_CLEAN("== Testing shutdown with clients in list");

  hub_init();
  xcb_handler_init();
  client_list_component_init();

  /* Create some clients */
  xcb_create_notify_event_t e1 = {
    .response_type = 16, /* XCB_CREATE_NOTIFY */
    .window        = 5001,
  };
  xcb_handler_dispatch(&e1);

  xcb_create_notify_event_t e2 = {
    .response_type = 16, /* XCB_CREATE_NOTIFY */
    .window        = 5002,
  };
  xcb_handler_dispatch(&e2);

  assert(client_list_count() == 2);

  /* Shutdown should clean up all clients */
  client_list_component_shutdown();

  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * The manage path asks whether the window belongs on screen before it maps it,
 * and the request carries the client's target id so the component that owns the
 * answer knows which client is being asked about. The responder here stands in
 * for the tag manager, which is the component that answers in the WM.
 */
static bool     visibility_answer = true;
static TargetID visibility_target = TARGET_ID_NONE;

static void
visibility_stub_executor(struct HubRequest* req)
{
  visibility_target = req->target;
  if (req->data != NULL)
    *(bool*) req->data = visibility_answer;
}

static HubComponent visibility_stub = {
  .name     = "visibility-stub",
  .requests = (RequestType[]) { REQ_CLIENT_VISIBLE, 0 },
  .executor = visibility_stub_executor,
};

/*
 * Test MAP_REQUEST maps a client only when its tags are on screen
 *
 * A client whose tags the view is not on is still managed: it keeps the border
 * and the list entry, and the component that said no maps it when a tag it is
 * on is shown.
 */
void
test_map_request_maps_only_a_client_on_screen(void)
{
  LOG_CLEAN("== Testing MAP_REQUEST maps a client only when its tags are on screen");

  hub_init();
  xcb_handler_init();
  client_list_component_init();
  assert(hub_register_component(&visibility_stub) == 0);

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 1,
    .parent        = 100,
    .window        = 800,
  };

  visibility_answer = false;
  visibility_target = TARGET_ID_NONE;
  xcb_handler_dispatch(&map_event);

  Client* off = client_get_by_window(800);
  assert_or_abort(off != NULL);
  assert(visibility_target == off->target.id);
  assert(client_is_managed(off) == true);
  assert(client_is_mapped(off) == false);
  assert(off->border_width == 1);

  /* The answer is not a property of the component: the next window, whose tags
   * are on the view, is mapped. */
  visibility_answer  = true;
  map_event.sequence = 2;
  map_event.window   = 801;
  xcb_handler_dispatch(&map_event);

  Client* on = client_get_by_window(801);
  assert_or_abort(on != NULL);
  assert(client_is_mapped(on) == true);

  hub_unregister_component(visibility_stub.name);
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * A window the float rules name starts out of the layout.
 *
 * The rules match the client's WM_CLASS, which the WM reads from the server, so
 * the manage path asks for it and places the window only after the answer. The
 * client keeps the rectangle it asked for: a window placed by the layout first
 * would keep the tiled slot it was given instead, and the rule would take it
 * back out of the layout with that geometry saved.
 *
 * The unit suite has no X connection, so the read is a no-op and the class the
 * test set is the one the rule sees - which is the state the manage path is in
 * once the reply has arrived.
 */
void
test_map_request_floats_a_client_a_rule_names(void)
{
  LOG_CLEAN("== Testing a float rule decides the placement at manage");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  floating_component_init();

  Monitor* m = monitor_create(90);
  assert_or_abort(m != NULL);
  monitor_set_geometry(m, 0, 0, 800, 600);

  xcb_create_notify_event_t create_event = {
    .response_type = 16, /* XCB_CREATE_NOTIFY */
    .window        = 500,
    .x             = 0,
    .y             = 0,
    .width         = 400,
    .height        = 300,
  };
  xcb_handler_dispatch(&create_event);

  Client* c = client_get_by_window(500);
  assert_or_abort(c != NULL);
  /* "steam" is one of the compiled rules, which are the ones in force with no
   * config file. */
  client_set_class(c, strdup("steam"));
  assert(c->class_name != NULL);

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 2,
    .parent        = 100,
    .window        = 500,
  };
  xcb_handler_dispatch(&map_event);

  assert(client_is_managed(c) == true);
  assert(floating_is_floating(c) == true);

  /* No layout has resized the window: it still covers what it asked for. */
  assert(c->width == 400);
  assert(c->height == 300);

  client_list_component_shutdown();
  floating_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Counts the tile requests the manage path sends for a window no rule names,
 * and the target each one was sent for.
 */
static TargetID tile_stub_target   = TARGET_ID_NONE;
static int      tile_stub_requests = 0;

static void
tile_stub_executor(struct HubRequest* req)
{
  tile_stub_target = req->target;
  tile_stub_requests++;
}

static HubComponent tile_stub = {
  .name     = "tile-stub",
  .requests = (RequestType[]) { REQ_MONITOR_TILE, 0 },
  .executor = tile_stub_executor,
};

/*
 * A window the rules do not name takes the tiled slot the manage path has
 * always given it, so the monitor is asked for a layout.
 */
void
test_map_request_tiles_a_client_no_rule_names(void)
{
  LOG_CLEAN("== Testing a window no float rule names is tiled");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  floating_component_init();
  assert(hub_register_component(&tile_stub) == 0);

  Monitor* m = monitor_create(91);
  assert_or_abort(m != NULL);
  monitor_set_geometry(m, 0, 0, 800, 600);

  xcb_create_notify_event_t create_event = {
    .response_type = 16, /* XCB_CREATE_NOTIFY */
    .window        = 600,
  };
  xcb_handler_dispatch(&create_event);

  Client* c = client_get_by_window(600);
  assert_or_abort(c != NULL);
  client_set_class(c, strdup("no-rule-names-this"));

  tile_stub_requests = 0;
  tile_stub_target   = TARGET_ID_NONE;

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 2,
    .parent        = 100,
    .window        = 600,
  };
  xcb_handler_dispatch(&map_event);

  assert(client_is_managed(c) == true);
  assert(floating_is_floating(c) == false);
  assert(tile_stub_requests == 1);
  assert(tile_stub_target == m->target.id);

  hub_unregister_component(tile_stub.name);
  client_list_component_shutdown();
  floating_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * A managed window receives input focus from the manage path.
 *
 * focus_on_client_managed() runs on EVT_CLIENT_MANAGED and refuses a client
 * that is not mapped. Emitting that event before the map left every new window
 * unfocused unless an EnterNotify happened to follow. The unit suite has no X
 * connection, so the map is the client_set_mapped() side of show_if_on_screen;
 * the event has to follow it the same way it does with a server.
 */
void
test_map_request_focuses_the_managed_client(void)
{
  LOG_CLEAN("== Testing manage path focuses the window it mapped");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  monitor_list_init();
  client_list_component_init();
  focus_component_init();

  Monitor* m = monitor_create(92);
  assert_or_abort(m != NULL);
  monitor_set_geometry(m, 0, 0, 800, 600);

  xcb_map_request_event_t map_event = {
    .response_type = 20, /* XCB_MAP_REQUEST */
    .sequence      = 1,
    .parent        = 100,
    .window        = 700,
  };
  xcb_handler_dispatch(&map_event);

  Client* c = client_get_by_window(700);
  assert_or_abort(c != NULL);
  assert(client_is_managed(c) == true);
  assert(client_is_mapped(c) == true);
  assert(focus_is_focused(c) == true);

  focus_component_shutdown();
  client_list_component_shutdown();
  monitor_list_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

TEST_GROUP(ClientListComponent, {
  test_client_list_component_init();
  test_client_list_component_shutdown();
  test_create_notify_creates_client();
  test_create_notify_skips_override_redirect();
  test_destroy_notify_destroys_client();
  test_map_request_manages_client();
  test_managed_event_mask_covers_the_readers();
  test_property_notify_ignores_other_properties();
  test_map_request_sets_border_width();
  test_map_request_creates_client_if_not_exists();
  test_map_request_maps_only_a_client_on_screen();
  test_map_request_floats_a_client_a_rule_names();
  test_map_request_tiles_a_client_no_rule_names();
  test_map_request_focuses_the_managed_client();
  test_unmap_notify_unmanages_client();
  test_unmap_notify_of_a_wm_unmap_keeps_the_client();
  test_unmap_notify_of_a_hide_after_a_show_keeps_the_client();
  test_synthetic_unmap_notify_of_a_hidden_client_unmanages();
  test_managed_client_lands_on_monitor();
  test_multiple_clients();
  test_client_list_utility_functions();
  test_client_adopts_components_on_creation();
  test_shutdown_with_clients();
});