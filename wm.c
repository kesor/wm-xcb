#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wm-log.h"
#include "wm-running.h"
#include "wm-signals.h"

#include "wm-hub.h"
#include "wm-states.h"
#include "wm-xcb-ewmh.h"
#include "wm-xcb.h"

#include "src/components/client-list.h"
#include "src/components/fullscreen.h"
#include "src/components/keybinding.h"
#include "src/components/monitor-manager.h"
#include "src/components/pertag.h"
#include "src/components/tiling.h"

#include "src/actions/launcher.h"

#include "wm.h"

int
main(int argc, char** argv)
{
  /* Initialize core systems */
  setup_signals();
  setup_xcb();
  setup_ewmh();
  setup_state_machine();

  /* Initialize hub first */
  hub_init();

  /* Initialize components */
  fullscreen_component_init();
  keybinding_init();
  client_list_component_init();
  /* Initialize pertag BEFORE monitor_manager so monitors get adopted */
  pertag_component_init();
  monitor_manager_init();
  tiling_component_init();

  /* Initialize launcher action (must be after action_registry_init which happens in keybinding_init) */
  launcher_init();

  /* Main event loop */
  while (running) {
    handle_xcb_events();
  }

  /* Shutdown in reverse order */
  monitor_manager_shutdown();
  tiling_component_shutdown();
  client_list_component_shutdown();
  keybinding_shutdown();
  /* launcher_shutdown is called by keybinding_shutdown via action_registry_shutdown */
  fullscreen_component_shutdown();
  /* pertag shutdown happens after monitor_manager because monitors unregister first */
  hub_shutdown();
  pertag_component_shutdown();

  /* Cleanup core systems */
  destruct_state_machine();
  destruct_ewmh();
  destruct_xcb();
  return 0;
}
