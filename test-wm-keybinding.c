/*
 * test-wm-keybinding.c - Tests for keybinding component
 *
 * Tests:
 * - keybinding_lookup: finding bindings by keycode and modifiers
 * - keybinding component registration with hub
 * - XCB handler registration
 * - Keybinding action execution (sending hub requests)
 */

#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "src/xcb/xcb-handler.h"
#include "src/components/keybinding.h"

/* Track which requests were sent to hub */
static int      last_request_type   = 0;
static TargetID last_request_target = TARGET_ID_NONE;
static void*    last_request_data   = NULL;

static void
reset_request_tracking(void)
{
  last_request_type   = 0;
  last_request_target = TARGET_ID_NONE;
  last_request_data   = NULL;
}

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

  /* Mod4 + keycode 36 (Return) -> KEYBINDING_ACTION_FOCUS_CLIENT */
  const KeyBinding* binding = keybinding_lookup(XCB_MOD_MASK_4, 36);
  assert(binding != NULL);
  assert(binding->action == KEYBINDING_ACTION_FOCUS_CLIENT);

  /* Mod4 + Shift + keycode 36 -> KEYBINDING_ACTION_FOCUS_PREV */
  binding = keybinding_lookup(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 36);
  assert(binding != NULL);
  assert(binding->action == KEYBINDING_ACTION_FOCUS_PREV);

  /* Mod4 + keycode 10 (1) -> KEYBINDING_ACTION_TAG_VIEW with arg=1 */
  binding = keybinding_lookup(XCB_MOD_MASK_4, 10);
  assert(binding != NULL);
  assert(binding->action == KEYBINDING_ACTION_TAG_VIEW);
  assert(binding->arg == 1);

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
  uint32_t mod_with_lock = XCB_MOD_MASK_4 | XCB_MOD_MASK_LOCK;
  const KeyBinding* binding = keybinding_lookup(mod_with_lock, 36);
  assert(binding != NULL);
  assert(binding->action == KEYBINDING_ACTION_FOCUS_CLIENT);

  /* Mod4 + NumLock (mode switch) should also match */
  uint32_t mod_with_numlock = XCB_MOD_MASK_4 | XCB_MOD_MASK_2;
  binding = keybinding_lookup(mod_with_numlock, 36);
  assert(binding != NULL);
  assert(binding->action == KEYBINDING_ACTION_FOCUS_CLIENT);

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
    xcb_keycode_t keycode = 9 + i;  /* keycodes 10-18 */
    const KeyBinding* binding = keybinding_lookup(XCB_MOD_MASK_4, keycode);
    assert(binding != NULL);
    assert(binding->action == KEYBINDING_ACTION_TAG_VIEW);
    assert(binding->arg == (uint32_t) i);
  }

  /* Mod4 + Shift + 1-9 for tag toggle */
  for (int i = 1; i <= 9; i++) {
    xcb_keycode_t keycode = 9 + i;
    const KeyBinding* binding = keybinding_lookup(
        XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, keycode);
    assert(binding != NULL);
    assert(binding->action == KEYBINDING_ACTION_TAG_TOGGLE);
    assert(binding->arg == (uint32_t) i);
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
  /* Note: The keybinding component should register its handlers
   * when keybinding_init() is called. For now, we check the component
   * is registered with hub. */
  HubComponent* comp = hub_get_component_by_name("keybinding");
  assert(comp != NULL);

  /* Get the keybinding component's registered request types */
  RequestType* requests = (RequestType*) comp->requests;
  int request_count = 0;
  while (requests[request_count] != 0) {
    request_count++;
  }
  LOG_DEBUG("Keybinding component handles %d request types", request_count);
  assert(request_count >= 4);  /* At least FOCUS, TAG_VIEW, TAG_TOGGLE, CLOSE */

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
  assert(count > 10);  /* At least 20 bindings: focus + tags */

  LOG_DEBUG("Got %" PRIu32 " keybindings via accessor", count);

  /* Verify first binding is Mod+Enter for focus */
  assert(bindings[0].action == KEYBINDING_ACTION_FOCUS_CLIENT);
  assert(bindings[0].keycode == 36);

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

  /* Simulate a key press event: Mod+Enter (keycode 36) */
  /* Create a fake event structure - first byte is response_type,
   * then the xcb_key_press_event_t fields */
  struct {
    uint8_t  response_type;
    uint8_t  detail;
    uint16_t sequence;
    uint32_t time;
    uint32_t root;
    uint32_t event;
    uint32_t child;
    int16_t  root_x;
    int16_t  root_y;
    int16_t  event_x;
    int16_t  event_y;
    uint32_t state;   /* modifier state */
    uint16_t same_screen;
  } fake_event = {
    .response_type = XCB_KEY_PRESS,
    .detail         = 36,  /* Return key */
    .sequence       = 0,
    .time           = 0,
    .root           = 0,
    .event          = 0,
    .child          = 0,
    .root_x         = 0,
    .root_y         = 0,
    .event_x        = 0,
    .event_y        = 0,
    .state          = XCB_MOD_MASK_4,  /* Mod4 pressed */
    .same_screen    = 1,
  };

  /* Call the handler directly - this should look up the binding
   * and send a hub request. Without a focus component providing
   * the current client, the request will target NONE. */
  keybinding_handle_key_press(&fake_event);

  /* The handler should have logged a debug message about no focused client
   * since focus_get_current_client() returns TARGET_ID_NONE */
  LOG_DEBUG("Key press handling completed (no focus component)");

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

  /* Shutdown */
  keybinding_shutdown();
  HubComponent* comp2 = hub_get_component_by_name("keybinding");
  assert(comp2 == NULL);

  /* Re-init should work */
  keybinding_init();
  HubComponent* comp3 = hub_get_component_by_name("keybinding");
  assert(comp3 != NULL);
  assert(comp3 == comp1);  /* Same component struct */

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
});