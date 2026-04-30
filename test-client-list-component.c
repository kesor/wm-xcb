/*
 * Client List Component Tests
 *
 * Tests for the client-list component that handles XCB events
 * for client lifecycle management.
 */

#include "src/components/client-list.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"

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
  assert(t != NULL && t->type == TARGET_TYPE_CLIENT);

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

  /* Client should now be managed */
  assert(c != NULL && client_is_managed(c) == true);

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

  /* Send UNMAP_NOTIFY */
  xcb_unmap_notify_event_t unmap_event = {
    .response_type  = 18, /* XCB_UNMAP_NOTIFY */
    .sequence       = 3,
    .event          = 700,
    .window         = 700,
    .from_configure = 0,
  };
  xcb_handler_dispatch(&unmap_event);

  /* Client should no longer be managed */
  assert(c != NULL && client_is_managed(c) == false);

  /* Client still exists (just unmanaged) */
  assert(client_list_count() == 1);

  /* Cleanup */
  client_list_component_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
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
      .x                 = (i * 10),
      .y                 = (i * 10),
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
  assert(t != NULL && t->type == TARGET_TYPE_CLIENT);

  /* Cleanup */
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

TEST_GROUP(ClientListComponent, {
  test_client_list_component_init();
  test_client_list_component_shutdown();
  test_create_notify_creates_client();
  test_create_notify_skips_override_redirect();
  test_destroy_notify_destroys_client();
  test_map_request_manages_client();
  test_map_request_creates_client_if_not_exists();
  test_unmap_notify_unmanages_client();
  test_multiple_clients();
  test_client_list_utility_functions();
  test_client_adopts_components_on_creation();
  test_shutdown_with_clients();
});