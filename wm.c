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
  monitor_manager_init();
  pertag_component_init();

  /* Main event loop */
  while (running) {
    handle_xcb_events();
  }

  /* Shutdown in reverse order */
  monitor_manager_shutdown();
  client_list_component_shutdown();
  keybinding_shutdown();
  fullscreen_component_shutdown();
  pertag_component_shutdown();

  hub_shutdown();

  /* Cleanup core systems */
  destruct_state_machine();
  destruct_ewmh();
  destruct_xcb();
  return 0;
}
