#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/core/wm-log.h"
#include "src/core/wm-running.h"
#include "src/core/wm-signals.h"

#include "src/config/config.h"
#include "src/core/wm-components.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-xcb-ewmh.h"
#include "src/core/wm-xcb.h"

#include "src/core/wm.h"

int
main(int argc, char** argv)
{
  /* Initialize core systems */
  setup_signals();
  setup_xcb();
  setup_ewmh();

  /* Initialize hub first */
  hub_init();

  /* Before the components: the keybinding component wires its table from
   * whatever this kept, and a missing file leaves the compiled bindings. */
  config_load_runtime(NULL);

  if (!wm_components_init()) {
    LOG_ERROR("The window manager did not start: a component failed to initialize");
    wm_components_shutdown();
    config_shutdown();
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

    /* A SIGHUP asked for the config file to be reread. The signal handler only
     * set a flag; the reload runs here, where the binding table and the X
     * connection are both in a state it can use. */
    if (signals_take_reload_request())
      (void) config_reload();

    handle_xcb_events();
  }

  wm_components_shutdown();

  /* The binding table held pointers into the file, and keybinding_shutdown()
   * has emptied it. */
  config_shutdown();

  /* Cleanup core systems */
  destruct_ewmh();
  destruct_xcb();
  return 0;
}
