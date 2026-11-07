/*
 * fuzz-xcb-event.c - feed arbitrary bytes to the WM's X event dispatch.
 *
 * The event loop hands a 32-byte X event to xcb_handler_dispatch(), which masks
 * the response type and calls whichever component registered for it. Every
 * handler reads fields out of that buffer without re-checking its length,
 * because the server is the one that set them. A wrong length, a count field
 * that says a thousand items follow, a property type nobody expected - all of it
 * is the server's word, and the WM trusts it.
 *
 * This harness takes the server's word from the fuzzer instead. It builds with
 * clang, ASan and UBSan, so a read past the end of an event is a crash with a
 * stack, not a silently mis-read field.
 *
 * Build and run: make fuzz
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/components/keybinding.h"
#include "src/components/monitor-manager.h"
#include "src/components/tag-manager.h"
#include "src/core/wm-hub.h"
#include "src/xcb/xcb-handler.h"

/* X events are 32 bytes. The dispatch reads the first, handlers read the rest. */
#define EVENT_BYTES 32

int
LLVMFuzzerInitialize(int* argc, char*** argv)
{
  (void) argc;
  (void) argv;

  /* The components only need to be registered; there is no X connection here,
     so a handler that asks the server early-returns, which is itself a path
     worth fuzzing. */
  hub_init();
  xcb_handler_init();
  action_registry_init();

  client_list_init();
  focus_component_init();
  fullscreen_component_init();
  keybinding_init();
  monitor_manager_init();
  tag_manager_component_init();
  return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  uint8_t event[EVENT_BYTES];

  memset(event, 0, sizeof event);
  memcpy(event, data, size < sizeof event ? size : sizeof event);

  xcb_handler_dispatch(event);
  return 0;
}
