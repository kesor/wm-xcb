#include "wm-components.h"

#include "wm-hub.h"
#include "wm-log.h"

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/components/keybinding.h"
#include "src/components/monitor-manager.h"
#include "src/components/pertag.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"

#include "src/actions/launcher.h"
#include "src/actions/terminal.h"

/*
 * What the startup check looks for afterwards. wm_components_init() and the
 * startup test read this one list, so a module added here is checked by both
 * and a module added to the init calls below without an entry here is the only
 * way to get an unchecked one.
 */
static const WiredModule wired_components[] = {
  { KEYBINDING_COMPONENT_NAME,      keybinding_is_initialized            },
  { FULLSCREEN_COMPONENT_NAME,      fullscreen_component_is_initialized  },
  { CLIENT_LIST_COMPONENT_NAME,     client_list_component_is_initialized },
  { PERTAG_COMPONENT_NAME,          pertag_component_is_initialized      },
  { TAG_MANAGER_COMPONENT_NAME,     tag_manager_component_is_initialized },
  { MONITOR_MANAGER_COMPONENT_NAME, monitor_manager_is_initialized       },
  { FOCUS_COMPONENT_NAME,           focus_component_is_initialized       },
  { TILING_COMPONENT_NAME,          tiling_component_is_initialized      },
  { NULL,                           NULL                                 },
};

static const WiredModule wired_actions[] = {
  { "app.launch",     launcher_is_initialized },
  { "terminal.spawn", terminal_is_initialized },
  { NULL,             NULL                    },
};

const WiredModule*
wm_components_wired(void)
{
  return wired_components;
}

const WiredModule*
wm_components_wired_actions(void)
{
  return wired_actions;
}

bool
wm_components_init(void)
{
  /* keybinding_init creates the action registry. fullscreen_component_init
   * registers an action and aborts the component if that fails, so it follows. */
  keybinding_init();
  fullscreen_component_init();
  client_list_component_init();
  /* pertag, tag-manager, and tiling store state on monitors, and adoption runs
   * when the monitor target is registered. All three go before
   * monitor_manager_init() creates the monitors: a component registered after
   * the discovery never reaches the monitors it already created. */
  pertag_component_init();
  tag_manager_component_init();
  tiling_component_init();
  monitor_manager_init();
  focus_component_init();

  /* Both actions need the registry keybinding_init created */
  launcher_init();
  terminal_init();

  for (const WiredModule* module = wired_components; module->name != NULL; module++) {
    if (hub_get_component_by_name(module->name) == NULL) {
      LOG_ERROR("Component '%s' did not register; the window manager cannot run without it",
                module->name);
      return false;
    }
    if (!module->is_initialized()) {
      LOG_ERROR("Component '%s' registered but is not initialized", module->name);
      return false;
    }
  }
  for (const WiredModule* module = wired_actions; module->name != NULL; module++) {
    if (!action_exists(module->name)) {
      LOG_ERROR("Action '%s' was not registered; its module did not come up", module->name);
      return false;
    }
    if (!module->is_initialized()) {
      LOG_ERROR("Action '%s' exists but its module is not initialized", module->name);
      return false;
    }
  }
  return true;
}

void
wm_components_shutdown(void)
{
  /* Clients hold state machines that point at their component's cached
   * template, so the client list goes before the components those machines
   * come from. Unregistering an action needs the registry, so every component
   * goes before keybinding_shutdown(). */
  monitor_manager_shutdown();
  tag_manager_component_shutdown();
  tiling_component_shutdown();
  client_list_component_shutdown();
  focus_component_shutdown();
  fullscreen_component_shutdown();
  /* Action modules go before the registry: the launcher also drops the pipe
   * it registered with the loop and closes it, which would otherwise happen
   * after the loop has forgotten every descriptor. */
  launcher_shutdown();
  terminal_shutdown();
  keybinding_shutdown();
  /* pertag shutdown happens after monitor_manager because monitors unregister first */
  hub_shutdown();
  pertag_component_shutdown();
}
