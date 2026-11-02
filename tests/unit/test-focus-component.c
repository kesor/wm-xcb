/*
 * Focus Component Tests
 *
 * Tests for the focus component that handles XCB events
 * for client focus management.
 */

#include <string.h>
#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-target-client.h"
#include "test-wm.h"
#include "wm-hub.h"

/*
 * Event tracking state for focus tests
 */
static struct {
  bool     focused_received;
  bool     unfocused_received;
  TargetID focused_target;
  TargetID unfocused_target;
} focus_test_events;

/*
 * Event handler for focus tests - uses static tracking
 */
static void
focus_test_event_handler(Event e)
{
  if (e.type == EVT_CLIENT_FOCUSED) {
    focus_test_events.focused_received = true;
    focus_test_events.focused_target   = e.target;
  } else if (e.type == EVT_CLIENT_UNFOCUSED) {
    focus_test_events.unfocused_received = true;
    focus_test_events.unfocused_target   = e.target;
  }
}

/*
 * Test component initialization
 */
void
test_focus_component_init(void)
{
  LOG_CLEAN("== Testing focus component init");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();

  /* Initially not initialized */
  assert(focus_component_is_initialized() == false);

  /* Initialize should succeed */
  bool result = focus_component_init();
  assert(result == true);
  assert(focus_component_is_initialized() == true);

  /* Re-init should be safe */
  result = focus_component_init();
  assert(result == true);

  /* Should have registered XCB handlers */
  assert(xcb_handler_count_for_type(XCB_ENTER_NOTIFY) >= 1);
  assert(xcb_handler_count_for_type(XCB_LEAVE_NOTIFY) >= 1);

  /* Should have registered with hub */
  HubComponent* comp = hub_get_component_by_name(FOCUS_COMPONENT_NAME);
  assert(comp != NULL);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test component shutdown
 */
void
test_focus_component_shutdown(void)
{
  LOG_CLEAN("== Testing focus component shutdown");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Verify handlers are registered */
  assert(xcb_handler_count_for_type(XCB_ENTER_NOTIFY) >= 1);

  focus_component_shutdown();
  action_registry_shutdown();

  /* Handlers should be unregistered */
  assert(xcb_handler_count_for_type(XCB_ENTER_NOTIFY) == 0);
  assert(xcb_handler_count_for_type(XCB_LEAVE_NOTIFY) == 0);

  /* Should be uninitialized */
  assert(focus_component_is_initialized() == false);

  /* Component should be unregistered from hub */
  HubComponent* comp = hub_get_component_by_name(FOCUS_COMPONENT_NAME);
  assert(comp == NULL);

  xcb_handler_shutdown();
  sm_registry_shutdown();
  client_list_shutdown();
  hub_shutdown();
}

/*
 * Test focus state machine template creation
 */
void
test_focus_sm_template_create(void)
{
  LOG_CLEAN("== Testing focus SM template creation");

  SMTemplate* tmpl = focus_sm_template_create();
  assert_or_abort(tmpl != NULL);

  /* Check template has correct name */
  assert(strcmp(tmpl->name, "focus") == 0);

  /* Check template has 2 states */
  assert(tmpl->num_states == 2);

  /* Check template has 2 transitions */
  assert(tmpl->num_transitions == 2);

  /* Check initial state is UNFOCUSED */
  assert(tmpl->initial_state == FOCUS_STATE_UNFOCUSED);

  sm_template_destroy(tmpl);
}

/*
 * Test focus state machine for client
 */
void
test_focus_sm_for_client(void)
{
  LOG_CLEAN("== Testing focus SM for client");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Create a client */
  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  assert(c->window == 100);

  /* Initially no focus SM */
  assert(client_get_sm(c, "focus") == NULL);

  /* Get focus SM - should create it on demand */
  StateMachine* sm = focus_get_sm(c);
  assert_or_abort(sm != NULL);
  assert(client_get_sm(c, "focus") == sm);

  /* Check initial state is UNFOCUSED */
  assert(focus_get_state(c) == FOCUS_STATE_UNFOCUSED);
  assert(sm_get_state(sm) == FOCUS_STATE_UNFOCUSED);

  /* Check is_focused returns false */
  assert(focus_is_focused(c) == false);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test focus via raw_write
 */
void
test_focus_raw_write(void)
{
  LOG_CLEAN("== Testing focus raw_write");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Create a client */
  Client* c = client_create(100);
  assert_or_abort(c != NULL);

  /* Get focus SM */
  StateMachine* sm = focus_get_sm(c);
  assert_or_abort(sm != NULL);

  /* Initially unfocused */
  assert(focus_get_state(c) == FOCUS_STATE_UNFOCUSED);

  /* Set to focused via raw_write */
  focus_set_state(c, FOCUS_STATE_FOCUSED);
  assert(focus_get_state(c) == FOCUS_STATE_FOCUSED);
  assert(focus_is_focused(c) == true);

  /* Set back to unfocused via raw_write */
  focus_set_state(c, FOCUS_STATE_UNFOCUSED);
  assert(focus_get_state(c) == FOCUS_STATE_UNFOCUSED);
  assert(focus_is_focused(c) == false);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test ENTER_NOTIFY handler sets focus
 */
void
test_enter_notify_sets_focus(void)
{
  LOG_CLEAN("== Testing ENTER_NOTIFY sets focus");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Create two clients */
  Client* c1 = client_create(100);
  assert_or_abort(c1 != NULL);
  Client* c2 = client_create(200);
  assert_or_abort(c2 != NULL);

  /* Ensure both are managed (so they're focusable) */
  client_set_managed(c1, true);
  client_set_managed(c2, true);

  /* Create ENTER_NOTIFY event for client 1 */
  xcb_enter_notify_event_t enter_event = {
    .response_type = XCB_ENTER_NOTIFY,
    .detail        = XCB_NOTIFY_DETAIL_ANCESTOR,
    .mode          = XCB_NOTIFY_MODE_NORMAL,
    .event         = 100,
    .child         = 0,
    .root          = 0,
    .root_x        = 0,
    .root_y        = 0,
    .event_x       = 0,
    .event_y       = 0,
  };

  /* Initially no focus */
  assert(focus_get_focused_window() == 0);

  /* Handle enter notify for client 1 */
  focus_on_enter_notify(&enter_event);

  /* Client 1 should now be focused */
  assert(focus_get_focused_window() == 100);
  assert(focus_is_focused(c1) == true);
  assert(focus_is_focused(c2) == false);

  /* Create ENTER_NOTIFY event for client 2 */
  enter_event.event = 200;
  focus_on_enter_notify(&enter_event);

  /* Client 2 should now be focused */
  assert(focus_get_focused_window() == 200);
  assert(focus_is_focused(c2) == true);
  assert(focus_is_focused(c1) == false);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test LEAVE_NOTIFY handler clears focus
 */
void
test_leave_notify_clears_focus(void)
{
  LOG_CLEAN("== Testing LEAVE_NOTIFY clears focus");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Create a client */
  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);

  /* Simulate focus by setting state directly */
  focus_set_state(c, FOCUS_STATE_FOCUSED);
  assert(focus_is_focused(c) == true);

  /* Create LEAVE_NOTIFY event */
  xcb_leave_notify_event_t leave_event = {
    .response_type = XCB_LEAVE_NOTIFY,
    .detail        = XCB_NOTIFY_DETAIL_ANCESTOR,
    .mode          = XCB_NOTIFY_MODE_NORMAL,
    .event         = 100,
    .child         = 0,
    .root          = 0,
    .root_x        = 0,
    .root_y        = 0,
    .event_x       = 0,
    .event_y       = 0,
  };

  /* Handle leave notify */
  focus_on_leave_notify(&leave_event);

  /* Client should now be unfocused */
  assert(focus_is_focused(c) == false);
  assert(focus_get_focused_window() == 0);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test focus event emission
 */
void
test_focus_event_emission(void)
{
  LOG_CLEAN("== Testing focus event emission");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Reset tracking */
  memset(&focus_test_events, 0, sizeof(focus_test_events));

  /* Subscribe to focus events */
  hub_subscribe(EVT_CLIENT_FOCUSED, focus_test_event_handler, NULL);
  hub_subscribe(EVT_CLIENT_UNFOCUSED, focus_test_event_handler, NULL);

  /* Create a client */
  Client* c = client_create(100);
  assert(c != NULL);
  client_set_managed(c, true);

  /* Initially unfocused */
  assert(focus_is_focused(c) == false);

  /* Set to focused via focus_set_state (which calls sm_raw_write and emits) */
  focus_set_state(c, FOCUS_STATE_FOCUSED);
  assert(focus_is_focused(c) == true);
  assert(focus_test_events.focused_received == true);
  assert(focus_test_events.focused_target == (TargetID) 100);

  /* Reset tracking */
  memset(&focus_test_events, 0, sizeof(focus_test_events));

  /* Set back to unfocused (emits EVT_CLIENT_UNFOCUSED) */
  focus_set_state(c, FOCUS_STATE_UNFOCUSED);
  assert(focus_is_focused(c) == false);
  assert(focus_test_events.unfocused_received == true);
  assert(focus_test_events.unfocused_target == (TargetID) 100);

  /* Cleanup */
  hub_unsubscribe(EVT_CLIENT_FOCUSED, focus_test_event_handler);
  hub_unsubscribe(EVT_CLIENT_UNFOCUSED, focus_test_event_handler);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test focus guard functions
 */
void
test_focus_guards(void)
{
  LOG_CLEAN("== Testing focus guards");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Create a non-focusable client */
  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  client_set_focusable(c, false);

  /* Get focus SM */
  StateMachine* sm = focus_get_sm(c);
  assert_or_abort(sm != NULL);

  /* Focus guard should reject because client is not focusable */
  assert(focus_guard_can_focus(sm, NULL) == false);

  /* Make client focusable */
  client_set_focusable(c, true);
  assert(focus_guard_can_focus(sm, NULL) == true);

  /* Unfocus guard should always allow */
  assert(focus_guard_can_unfocus(sm, NULL) == true);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test the action names the default config wires for this component.
 */
void
test_focus_actions_registered(void)
{
  LOG_CLEAN("== Testing focus actions are registered");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  focus_component_init();

  Action* current = action_lookup("focus.focus-current");
  assert(current != NULL);
  if (current != NULL) {
    assert(current->callback != NULL);
    assert(current->target_type == ACTION_TARGET_NONE);
    assert(current->target_required == false);
  }

  Action* prev = action_lookup("focus.focus-prev");
  assert(prev != NULL);
  if (prev != NULL) {
    assert(prev->callback != NULL);
    assert(prev->target_type == ACTION_TARGET_NONE);
    assert(prev->target_required == false);
  }

  focus_component_shutdown();

  /* Shutdown must take the names with it, or a restart cannot register them */
  assert(action_lookup("focus.focus-current") == NULL);
  assert(action_lookup("focus.focus-prev") == NULL);

  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test that focus.focus-current picks the newest client that can take focus,
 * and that the resolved target is the one every client-targeted action sees.
 */
void
test_focus_current_action(void)
{
  LOG_CLEAN("== Testing focus.focus-current action");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* older = client_create(100);
  assert_or_abort(older != NULL);
  Client* newer = client_create(200);
  assert_or_abort(newer != NULL);

  /* A window on a hidden tag is not a focus target. */
  Client* hidden = client_create(300);
  assert_or_abort(hidden != NULL);

  client_set_managed(older, true);
  client_set_focusable(older, true);
  client_set_mapped(older, true);
  client_set_managed(newer, true);
  client_set_focusable(newer, true);
  client_set_mapped(newer, true);
  client_set_managed(hidden, true);
  client_set_focusable(hidden, true);
  client_set_mapped(hidden, false);

  assert(action_invoke("focus.focus-current", NULL) == true);
  assert(focus_get_focused_window() == 200);

  /* Client-targeted actions resolve through the same focus state. */
  assert(action_resolve_current_client() == newer->target.id);
  assert(action_get_current_client() == newer);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test that focus.focus-prev walks to the older client and wraps around.
 */
void
test_focus_prev_action(void)
{
  LOG_CLEAN("== Testing focus.focus-prev action");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* older = client_create(100);
  assert_or_abort(older != NULL);
  Client* newer = client_create(200);
  assert_or_abort(newer != NULL);

  client_set_managed(older, true);
  client_set_focusable(older, true);
  client_set_mapped(older, true);
  client_set_managed(newer, true);
  client_set_focusable(newer, true);
  client_set_mapped(newer, true);

  assert(action_invoke("focus.focus-current", NULL) == true);
  assert(focus_get_focused_window() == 200);

  assert(action_invoke("focus.focus-prev", NULL) == true);
  assert(focus_get_focused_window() == 100);
  assert(focus_is_focused(older) == true);
  assert(focus_is_focused(newer) == false);

  /* The oldest client has no older neighbour; the ring wraps to the newest. */
  assert(action_invoke("focus.focus-prev", NULL) == true);
  assert(focus_get_focused_window() == 200);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test that un-managed clients are not focusable
 */
void
test_unmanaged_client_not_focusable(void)
{
  LOG_CLEAN("== Testing un-managed client not focusable");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  /* Create an un-managed client */
  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  client_set_managed(c, false);  /* Not managed */
  client_set_focusable(c, true); /* But marked as focusable */

  /* Get focus SM */
  StateMachine* sm = focus_get_sm(c);
  assert_or_abort(sm != NULL);

  /* Focus guard should reject because client is not managed */
  assert(focus_guard_can_focus(sm, NULL) == false);

  /* Cleanup */
  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * A client that is managed takes focus.
 *
 * Nothing else would point the server at a window mapped after startup: focus
 * otherwise follows the pointer, which was already somewhere else.
 */
void
test_focus_follows_managed_client(void)
{
  LOG_CLEAN("== Testing a managed client takes focus");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  client_set_mapped(c, true);

  assert(focus_get_focused_window() == 0);

  hub_emit(EVT_CLIENT_MANAGED, c->target.id, NULL);

  assert(focus_get_focused_window() == 100);
  assert(focus_is_focused(c) == true);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Focus moves to the newest remaining client when the focused one is unmapped.
 */
void
test_focus_moves_when_focused_client_unmaps(void)
{
  LOG_CLEAN("== Testing focus moves when the focused client unmaps");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* older = client_create(100);
  assert_or_abort(older != NULL);
  Client* newer = client_create(200);
  assert_or_abort(newer != NULL);

  client_set_managed(older, true);
  client_set_mapped(older, true);
  client_set_managed(newer, true);
  client_set_mapped(newer, true);

  /* The order client-list manages them: the older window first */
  hub_emit(EVT_CLIENT_MANAGED, older->target.id, NULL);
  hub_emit(EVT_CLIENT_MANAGED, newer->target.id, NULL);

  assert(focus_get_focused_window() == 200);

  /* The order client-list uses: the flags go first, then the event */
  client_set_managed(newer, false);
  client_set_mapped(newer, false);
  hub_emit(EVT_CLIENT_UNMANAGED, newer->target.id, NULL);

  assert(focus_get_focused_window() == 100);
  assert(focus_is_focused(older) == true);
  assert(focus_is_focused(newer) == false);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Focus moves off a client that is destroyed while it still looks managed and
 * mapped: the client is unregistered after the event, not before it.
 */
void
test_focus_moves_when_focused_client_is_destroyed(void)
{
  LOG_CLEAN("== Testing focus moves when the focused client is destroyed");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* older = client_create(100);
  assert_or_abort(older != NULL);
  Client* newer = client_create(200);
  assert_or_abort(newer != NULL);

  client_set_managed(older, true);
  client_set_mapped(older, true);
  client_set_managed(newer, true);
  client_set_mapped(newer, true);

  /* The order client-list manages them: the older window first */
  hub_emit(EVT_CLIENT_MANAGED, older->target.id, NULL);
  hub_emit(EVT_CLIENT_MANAGED, newer->target.id, NULL);

  assert(focus_get_focused_window() == 200);

  hub_emit(EVT_CLIENT_DESTROYED, newer->target.id, NULL);

  assert(focus_get_focused_window() == 100);
  assert(focus_is_focused(older) == true);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * The last client leaving leaves nothing focused, and its machine stops
 * claiming focus: the X focus goes back to the root window, which the unit
 * tests cannot see without a connection.
 */
void
test_focus_cleared_when_last_client_leaves(void)
{
  LOG_CLEAN("== Testing focus clears when the last client leaves");

  hub_init();
  sm_registry_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  client_set_mapped(c, true);
  hub_emit(EVT_CLIENT_MANAGED, c->target.id, NULL);
  assert(focus_is_focused(c) == true);

  client_set_managed(c, false);
  client_set_mapped(c, false);
  hub_emit(EVT_CLIENT_UNMANAGED, c->target.id, NULL);

  assert(focus_get_focused_window() == 0);
  assert(focus_is_focused(c) == false);

  focus_component_shutdown();
  action_registry_shutdown();
  client_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Register test group
 */
TEST_GROUP(FocusComponent, {
  test_focus_component_init();
  test_focus_component_shutdown();
  test_focus_sm_template_create();
  test_focus_sm_for_client();
  test_focus_raw_write();
  test_enter_notify_sets_focus();
  test_leave_notify_clears_focus();
  test_focus_event_emission();
  test_focus_guards();
  test_focus_actions_registered();
  test_focus_current_action();
  test_focus_prev_action();
  test_unmanaged_client_not_focusable();
  test_focus_follows_managed_client();
  test_focus_moves_when_focused_client_unmaps();
  test_focus_moves_when_focused_client_is_destroyed();
  test_focus_cleared_when_last_client_leaves();
});