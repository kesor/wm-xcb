#include "wm-components.h"

#include "wm-hub.h"

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

void
wm_components_init(void)
{
  /* keybinding_init creates the action registry. fullscreen_component_init
   * registers an action and aborts the component if that fails, so it follows. */
  keybinding_init();
  fullscreen_component_init();
  client_list_component_init();
  /* pertag and tag-manager store state machines on monitors, and adoption
   * happens when the monitor target is registered. Both go before
   * monitor_manager_init() creates the monitors. */
  pertag_component_init();
  tag_manager_component_init();
  monitor_manager_init();
  focus_component_init();
  tiling_component_init();

  /* Both actions need the registry keybinding_init created */
  launcher_init();
  terminal_init();
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
  keybinding_shutdown();
  /* pertag shutdown happens after monitor_manager because monitors unregister first */
  hub_shutdown();
  pertag_component_shutdown();
}
