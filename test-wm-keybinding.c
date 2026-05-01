/*
 * test-wm-keybinding.c - Tests for keybinding component
 *
 * Tests:
 * - keybinding_lookup: finding bindings by keycode and modifiers
 * - keybinding component registration with hub
 * - XCB handler registration
 * - Keybinding action execution (sending hub requests)
 */

#include <string.h>
#include "src/components/keybinding.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"
#include "wm-log.h"

/*
 * Test: keybinding component registers with hub
 */
void
test_keybinding_component_registration(void)
{
  LOG_CLEAN("== Testing keybinding component registration");

  hub_init();
  xcb_handler_init();

  keybinding_init();

  /* Check component was registered */
  HubComponent* comp = hub_get_component_by_name("keybinding");
  assert(comp != NULL);
  assert(comp == &keybinding_component);

  /* Check keybinding count */
  uint32_t count = keybinding_get_count();
  assert(count > 0);

  LOG_DEBUG("Keybinding component registered with %" PRIu32 " bindings", count);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: keybinding_lookup finds exact matches
 */
void
test_keybinding_lookup_exact(void)
{
  LOG_CLEAN("== Testing keybinding lookup with exact match");

  hub_init();
  xcb_handler_init();
  keybinding_init();

  /* Mod4 + keycode 36 (Return) -> focus.focus-current */
  const KeyBinding* binding = keybinding_lookup(XCB_MOD_MASK_4, 36);
  assert(binding != NULL && strcmp(binding->action, "focus.focus-current") == 0);

  /* Mod4 + Shift + keycode 36 -> focus.focus-prev */
  binding = keybinding_lookup(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 36);
  assert(binding != NULL && strcmp(binding->action, "focus.focus-prev") == 0);

  /* Mod4 + keycode 10 (1) -> tag-manager.view with arg=1 */
  binding = keybinding_lookup(XCB_MOD_MASK_4, 10);
  assert(binding != NULL && strcmp(binding->action, "tag-manager.view") == 0 && binding->arg == 1);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: keybinding_lookup returns NULL for unknown keys
 */
void
test_keybinding_lookup_unknown(void)
{
  LOG_CLEAN("== Testing keybinding lookup for unknown keys");

  hub_init();
  xcb_handler_init();
  keybinding_init();

  /* Random keycode with no binding */
  const KeyBinding* binding = keybinding_lookup(XCB_MOD_MASK_4, 99);
  assert(binding == NULL);

  /* No modifier keycode that has no binding */
  binding = keybinding_lookup(0, 20);
  assert(binding == NULL);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: modifiers are normalized (lock and mode switches ignored)
 */
void
test_keybinding_modifier_normalization(void)
{
  LOG_CLEAN("== Testing keybinding modifier normalization");

  hub_init();
  xcb_handler_init();
  keybinding_init();

  /* Mod4 + lock modifier should still match Mod4 alone */
  uint32_t          mod_with_lock = XCB_MOD_MASK_4 | XCB_MOD_MASK_LOCK;
  const KeyBinding* binding       = keybinding_lookup(mod_with_lock, 36);
  assert(binding != NULL && strcmp(binding->action, "focus.focus-current") == 0);

  /* Mod4 + NumLock (mode switch) should also match */
  uint32_t mod_with_numlock = XCB_MOD_MASK_4 | XCB_MOD_MASK_2;
  binding                   = keybinding_lookup(mod_with_numlock, 36);
  assert(binding != NULL && strcmp(binding->action, "focus.focus-current") == 0);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: tag bindings are looked up correctly
 */
void
test_keybinding_tag_bindings(void)
{
  LOG_CLEAN("== Testing tag keybinding lookup");

  hub_init();
  xcb_handler_init();
  keybinding_init();

  /* Mod4 + 1-9 for tag view */
  for (int i = 1; i <= 9; i++) {
    xcb_keycode_t     keycode = 9 + i; /* keycodes 10-18 */
    const KeyBinding* binding = keybinding_lookup(XCB_MOD_MASK_4, keycode);
    assert(binding != NULL && strcmp(binding->action, "tag-manager.view") == 0 && binding->arg == (uint32_t) i);
  }

  /* Mod4 + Shift + 1-9 for tag toggle */
  for (int i = 1; i <= 9; i++) {
    xcb_keycode_t     keycode = 9 + i;
    const KeyBinding* binding = keybinding_lookup(
        XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, keycode);
    assert(binding != NULL && strcmp(binding->action, "tag-manager.toggle") == 0 && binding->arg == (uint32_t) i);
  }

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: keybinding handler registration with XCB
 */
void
test_keybinding_xcb_handler_registration(void)
{
  LOG_CLEAN("== Testing keybinding XCB handler registration");

  hub_init();
  xcb_handler_init();

  /* Before keybinding init, no handlers registered */
  assert(xcb_handler_count_for_type(XCB_KEY_PRESS) == 0);
  assert(xcb_handler_count_for_type(XCB_KEY_RELEASE) == 0);

  /* Initialize keybinding - registers XCB handlers */
  keybinding_init();

  /* After init, handlers should be registered */
  HubComponent* comp = hub_get_component_by_name("keybinding");
  assert(comp != NULL);

  /* Verify XCB handlers were registered */
  assert(xcb_handler_count_for_type(XCB_KEY_PRESS) > 0);
  assert(xcb_handler_count_for_type(XCB_KEY_RELEASE) > 0);

  LOG_DEBUG("Keybinding registered %u KEY_PRESS and %u KEY_RELEASE handlers",
            xcb_handler_count_for_type(XCB_KEY_PRESS),
            xcb_handler_count_for_type(XCB_KEY_RELEASE));

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: keybinding get bindings accessor
 */
void
test_keybinding_get_bindings(void)
{
  LOG_CLEAN("== Testing keybinding get_bindings accessor");

  hub_init();
  keybinding_init();

  const KeyBinding* bindings = keybinding_get_bindings();
  assert(bindings != NULL);

  uint32_t count = keybinding_get_count();
  assert(count > 10); /* At least 20 bindings: focus + tags */

  LOG_DEBUG("Got %" PRIu32 " keybindings via accessor", count);

  /* Verify first binding is Mod+Enter for focus */
  const KeyBinding* first = keybinding_get_bindings();
  assert(first != NULL && strcmp(first->action, "focus.focus-current") == 0 && first->keycode == 36);

  keybinding_shutdown();
  hub_shutdown();
}

/*
 * Test: keybinding handle key press (simulated)
 */
void
test_keybinding_handle_key_press(void)
{
  LOG_CLEAN("== Testing keybinding key press handler");

  hub_init();
  xcb_handler_init();
  keybinding_init();

  /* Allocate and initialize a proper xcb_key_press_event_t structure */
  xcb_key_press_event_t* fake_event = calloc(1, sizeof(xcb_key_press_event_t));
  assert(fake_event != NULL);

  /* Set required fields with correct types */
  fake_event->response_type = XCB_KEY_PRESS;
  fake_event->detail        = 36;             /* Return key */
  fake_event->state         = XCB_MOD_MASK_4; /* Mod4 pressed */

  /* Call the handler directly */
  keybinding_handle_key_press(fake_event);

  LOG_DEBUG("Key press handling completed (no focus component)");

  free(fake_event);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: shutdown and re-init work correctly
 */
void
test_keybinding_shutdown_reinit(void)
{
  LOG_CLEAN("== Testing keybinding shutdown and reinit");

  hub_init();
  xcb_handler_init();

  /* First init */
  keybinding_init();
  HubComponent* comp1 = hub_get_component_by_name("keybinding");
  assert(comp1 != NULL);

  /* Verify handlers registered */
  uint32_t handlers_before = xcb_handler_count_for_type(XCB_KEY_PRESS);

  /* Shutdown */
  keybinding_shutdown();
  HubComponent* comp2 = hub_get_component_by_name("keybinding");
  assert(comp2 == NULL);

  /* Handlers should be unregistered */
  uint32_t handlers_after = xcb_handler_count_for_type(XCB_KEY_PRESS);
  assert(handlers_after < handlers_before);

  /* Re-init should work */
  keybinding_init();
  HubComponent* comp3 = hub_get_component_by_name("keybinding");
  assert(comp3 != NULL);
  assert(comp3 == comp1); /* Same component struct */

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: executor is NULL (keybinding doesn't handle requests)
 */
void
test_keybinding_no_executor(void)
{
  LOG_CLEAN("== Testing keybinding component has no executor");

  hub_init();
  xcb_handler_init();
  keybinding_init();

  HubComponent* comp = hub_get_component_by_name("keybinding");
  /* Verify component has no executor (sends requests only) */
  assert(comp != NULL && comp->requests == NULL && comp->executor == NULL);

  keybinding_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

TEST_GROUP(KeybindingComponent, {
  test_keybinding_component_registration();
  test_keybinding_lookup_exact();
  test_keybinding_lookup_unknown();
  test_keybinding_modifier_normalization();
  test_keybinding_tag_bindings();
  test_keybinding_xcb_handler_registration();
  test_keybinding_get_bindings();
  test_keybinding_handle_key_press();
  test_keybinding_shutdown_reinit();
  test_keybinding_no_executor();
});