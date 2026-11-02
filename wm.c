#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wm-log.h"
#include "wm-running.h"
#include "wm-signals.h"

#include "wm-components.h"
#include "wm-hub.h"
#include "wm-states.h"
#include "wm-xcb-ewmh.h"
#include "wm-xcb.h"

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

  if (!wm_components_init()) {
    LOG_ERROR("The window manager did not start: a component failed to initialize");
    wm_components_shutdown();
    destruct_state_machine();
    destruct_ewmh();
    destruct_xcb();
    return 1;
  }

  /* Main event loop: block until X or a signal has something, then dispatch
   * everything that arrived. */
  while (running) {
    wm_xcb_wait_for_events();
    handle_xcb_events();
  }

  wm_components_shutdown();

  /* Cleanup core systems */
  destruct_state_machine();
  destruct_ewmh();
  destruct_xcb();
  return 0;
}
