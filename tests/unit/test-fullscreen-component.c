/*
 * Fullscreen Component Tests
 *
 * Covers the action the default config binds to MOD+f: that the name is
 * registered with a client target, and that invoking it moves the focused
 * client's fullscreen state machine.
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/sm/sm-registry.h"
#include "src/xcb/xcb-handler.h"
#include "test-wm.h"
#include "wm-hub.h"

/*
 * Test: fullscreen.toggle is registered against the focused client.
 */
void
test_fullscreen_action_registered(void)
{
  LOG_CLEAN("== Testing fullscreen action registration");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  action_registry_init();
  fullscreen_component_init();

  Action* action = action_lookup("fullscreen.toggle");
  assert(action != NULL);
  if (action != NULL) {
    assert(action->callback != NULL);
    assert(action->target_type == ACTION_TARGET_CLIENT);
    assert(action->target_required == true);
  }

  fullscreen_component_shutdown();

  /* Shutdown must take the name with it, or a restart cannot register it */
  assert(action_lookup("fullscreen.toggle") == NULL);

  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: invoking the action toggles the client the focus component resolved.
 */
void
test_fullscreen_toggle_action(void)
{
  LOG_CLEAN("== Testing fullscreen.toggle action");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  focus_component_init();
  fullscreen_component_init();

  Client* c = client_create(700);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  client_set_focusable(c, true);
  client_set_mapped(c, true);
  focus_set_state(c, FOCUS_STATE_FOCUSED);

  assert(fullscreen_is_fullscreen(c) == false);

  assert(action_invoke("fullscreen.toggle", NULL) == true);
  assert(fullscreen_is_fullscreen(c) == true);

  assert(action_invoke("fullscreen.toggle", NULL) == true);
  assert(fullscreen_is_fullscreen(c) == false);

  /* With nothing focused the action has no target and must not pretend. */
  focus_set_state(c, FOCUS_STATE_UNFOCUSED);
  assert(action_invoke("fullscreen.toggle", NULL) == false);

  /* Clients own SMs that point at the component's template, so they go first */
  client_list_shutdown();
  fullscreen_component_shutdown();
  focus_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: registering the action is part of init. Without the registry the
 * component must not come up half-initialized, or a caller that runs before
 * keybinding_init drops fullscreen for the whole session.
 */
void
test_fullscreen_init_requires_action_registry(void)
{
  LOG_CLEAN("== Testing fullscreen init without the action registry");

  hub_init();
  sm_registry_init();
  xcb_handler_init();

  assert(fullscreen_component_init() == false);
  assert(fullscreen_component_is_initialized() == false);
  assert(hub_get_component_by_name(FULLSCREEN_COMPONENT_NAME) == NULL);

  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

TEST_GROUP(FullscreenComponent, {
  test_fullscreen_action_registered();
  test_fullscreen_toggle_action();
  test_fullscreen_init_requires_action_registry();
});
