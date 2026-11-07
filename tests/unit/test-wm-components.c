/*
 * Startup order tests
 *
 * wm_components_init() is the list of modules the window manager runs with, and
 * main() used to drop its failures on the floor: a component that returned early
 * left the WM running without it and no test noticed, because every component
 * test calls its own init and asserts around that.
 *
 * These tests run the sequence main() runs and then ask the modules themselves.
 * The expectation comes from wm_components_wired() and
 * wm_components_wired_actions(), the same lists wm_components_init() checks, so
 * a module added there is covered here without another edit.
 */

#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/components/keybinding.h"
#include "src/components/monitor-manager.h"
#include "src/components/pertag.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/core/wm-components.h"
#include "src/core/wm-hub.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"

/*
 * Test: every module on the startup list comes up.
 */
void
test_every_wired_module_starts(void)
{
  LOG_CLEAN("== Testing the startup order brings every wired module up");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  tag_list_init();
  monitor_list_init();
  /* A group that ran before this one can leave the action registry up, and
   * fullscreen would then register its action whatever order it runs in. main()
   * starts with no registry, and that is the state this test has to start in. */
  action_registry_shutdown();

  assert(wm_components_init() == true);

  for (const WiredModule* module = wm_components_wired(); module->name != NULL; module++) {
    LOG_CLEAN("  %s", module->name);
    assert(hub_get_component_by_name(module->name) != NULL);
    assert(module->is_initialized());
  }
  for (const WiredModule* module = wm_components_wired_actions(); module->name != NULL; module++) {
    LOG_CLEAN("  %s", module->name);
    assert(action_exists(module->name) == true);
    assert(module->is_initialized());
  }

  /* The bindings are wired before their owners register the actions, so the
   * check only means something after this sequence. Nothing in the compiled
   * configuration may name an action that is still missing. */
  assert(keybinding_validate_bindings() == 0);

  /* The focus component is the one this list was added for: its ENTER/LEAVE
   * handlers are registered inside focus_component_init(), so a startup that
   * skips it leaves the WM without pointer focus. */
  assert(xcb_handler_count_for_type(XCB_ENTER_NOTIFY) >= 1);
  assert(xcb_handler_count_for_type(XCB_LEAVE_NOTIFY) >= 1);
  assert(action_exists("focus.focus-current") == true);
  assert(action_exists("focus.focus-prev") == true);

  /* The other defect this list catches: fullscreen registers an action, so an
   * init order that starts it before the registry exists leaves no action
   * behind and wm_components_init() has to say so. */
  assert(action_exists("fullscreen.toggle") == true);

  wm_components_shutdown();

  for (const WiredModule* module = wm_components_wired(); module->name != NULL; module++) {
    LOG_CLEAN("  %s", module->name);
    assert(hub_get_component_by_name(module->name) == NULL);
    assert(module->is_initialized() == false);
  }
  for (const WiredModule* module = wm_components_wired_actions(); module->name != NULL; module++) {
    LOG_CLEAN("  %s", module->name);
    assert(action_exists(module->name) == false);
  }

  monitor_list_shutdown();
  tag_list_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Test: every component that adopts a monitor registers before the monitor
 * manager.
 *
 * Adoption runs when a monitor target is registered, and the monitor manager
 * registers the monitors the startup discovery finds. A component registered
 * after it never reaches those monitors: tiling was, and its layout state only
 * appeared on the first tile request, while a monitor that arrived later in the
 * session was adopted. The hub hands back compatible components in registration
 * order, so the position in that list is the registration order.
 */
void
test_monitor_components_register_before_the_monitor_manager(void)
{
  LOG_CLEAN("== Testing monitor components register before the monitor manager");

  hub_init();
  sm_registry_init();
  xcb_handler_init();
  tag_list_init();
  monitor_list_init();
  action_registry_shutdown();

  assert(wm_components_init() == true);

  HubComponent** comps      = hub_get_components_for_target_type_name("monitor");
  int32_t        manager_at = -1;
  for (uint32_t i = 0; comps != NULL && comps[i] != NULL; i++) {
    if (strcmp(comps[i]->name, MONITOR_MANAGER_COMPONENT_NAME) == 0) {
      manager_at = (int32_t) i;
      break;
    }
  }
  assert_or_abort(manager_at >= 0);

  for (uint32_t i = 0; comps[i] != NULL; i++) {
    if (comps[i]->on_adopt == NULL) {
      continue;
    }
    LOG_CLEAN("  %s", comps[i]->name);
    assert((int32_t) i < manager_at);
  }

  wm_components_shutdown();

  monitor_list_shutdown();
  tag_list_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/*
 * Register test group
 */
TEST_GROUP(WMComponents, {
  test_every_wired_module_starts();
  test_monitor_components_register_before_the_monitor_manager();
});
