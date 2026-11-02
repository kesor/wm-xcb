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
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-components.h"
#include "wm-hub.h"

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
 * Register test group
 */
TEST_GROUP(WMComponents, { test_every_wired_module_starts(); });
