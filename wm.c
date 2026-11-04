#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wm-log.h"
#include "wm-running.h"
#include "wm-signals.h"

#include "config.h"
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

  /* Before the components: the keybinding component wires its table from
   * whatever this kept, and a missing file leaves the compiled bindings. */
  config_load_runtime(NULL);

  if (!wm_components_init()) {
    LOG_ERROR("The window manager did not start: a component failed to initialize");
    wm_components_shutdown();
    config_shutdown();
    destruct_state_machine();
    destruct_ewmh();
    destruct_xcb();
    return 1;
  }

  /* Windows that were on the display before the WM started never send a
   * MapRequest, so they are adopted here, once the components that give a
   * client its state machines, monitor, and place in the layout are up. */
  wm_xcb_adopt_existing_windows();

  /* Main event loop: block until X or a signal has something, then dispatch
   * everything that arrived. */
  while (running) {
    wm_xcb_wait_for_events();
    handle_xcb_events();
  }

  wm_components_shutdown();

  /* The binding table held pointers into the file, and keybinding_shutdown()
   * has emptied it. */
  config_shutdown();

  /* Cleanup core systems */
  destruct_state_machine();
  destruct_ewmh();
  destruct_xcb();
  return 0;
}
