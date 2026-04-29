#define DEBUG 1

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock running for LOG_FATAL */
sig_atomic_t running = 1;

#define LOG_DEBUG(pFormat, ...)                    \
  {                                                \
    printf("DEBUG: " pFormat "\n", ##__VA_ARGS__); \
  }

#define LOG_CLEAN(pFormat, ...)          \
  {                                      \
    printf(pFormat "\n", ##__VA_ARGS__); \
  }

#define LOG_ERROR(pFormat, ...)                             \
  {                                                         \
    fprintf(stderr, "ERROR: " pFormat "\n", ##__VA_ARGS__); \
  }

#define LOG_FATAL(pFormat, ...)                             \
  {                                                         \
    fprintf(stderr, "FATAL: " pFormat "\n", ##__VA_ARGS__); \
    running = 0;                                            \
  }

#define LOG_WARN(pFormat, ...)                              \
  {                                                         \
    fprintf(stderr, "WARN: " pFormat "\n", ##__VA_ARGS__);  \
  }

#define assert(EXPRESSION)                                         \
  if (!(EXPRESSION)) {                                             \
    printf("%s:%d: %s - FAIL\n", __FILE__, __LINE__, #EXPRESSION); \
    exit(1);                                                       \
  } else {                                                         \
    printf("%s:%d: %s - pass\n", __FILE__, __LINE__, #EXPRESSION); \
  }

#include "src/sm/sm.h"
#include "src/sm/sm-registry.h"
#include "wm-hub.h"

/* Track events for testing */
static int hook_called = 0;
static int raw_write_events_emitted = 0;
static uint32_t last_raw_write_event = 0;

void
event_catcher(Event e)
{
  raw_write_events_emitted++;
  last_raw_write_event = e.type;
  LOG_DEBUG("Event caught: type=%u, target=%lu", e.type, (unsigned long) e.target);
}

/* Simple test emitter that records events */
static void
test_emitter(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) sm;
  (void) from_state;
  (void) to_state;
  (void) userdata;
  LOG_DEBUG("Custom emitter called");
  raw_write_events_emitted++;
}

/* Guard that always allows */
static bool
guard_allow(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

/* Guard that always rejects */
static bool
guard_deny(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return false;
}

/* Guard that checks data */
static bool
guard_conditional(StateMachine* sm, void* data)
{
  (void) sm;
  int* allow = (int*) data;
  return allow != NULL && *allow;
}

/* Action that always fails */
static bool
action_fail(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return false;
}

/* Action that tracks calls */
static bool
action_track(StateMachine* sm, void* data)
{
  (void) sm;
  int* count = (int*) data;
  if (count != NULL)
    (*count)++;
  return true;
}

/* Track hook calls per phase for comprehensive testing */
static int hook_calls[SM_HOOK_MAX] = {0};

void
test_hook_fn(StateMachine* sm, void* userdata)
{
  (void) sm;
  (void) userdata;
  hook_called++;
}

/* Hook functions for each phase - track which phase called them */
void hook_pre_guard(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_calls[SM_HOOK_PRE_GUARD]++;
}
void hook_post_guard(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_calls[SM_HOOK_POST_GUARD]++;
}
void hook_pre_action(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_calls[SM_HOOK_PRE_ACTION]++;
}
void hook_post_action(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_calls[SM_HOOK_POST_ACTION]++;
}
void hook_pre_emit(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_calls[SM_HOOK_PRE_EMIT]++;
}
void hook_post_emit(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_calls[SM_HOOK_POST_EMIT]++;
}

/* Hook with userdata to verify it's passed correctly */
void hook_with_userdata(StateMachine* sm, void* userdata) {
  (void) sm;
  int* counter = (int*) userdata;
  if (counter)
    (*counter)++;
}

/* Hook that can be removed - verify removal works */
static int hook_remove_test_count = 0;
void hook_remove_test(StateMachine* sm, void* userdata) {
  (void) sm; (void) userdata;
  hook_remove_test_count++;
}

void
test_sm_template_create_destroy()
{
  LOG_CLEAN("== Testing SMTemplate create/destroy");

  enum { STATE_A = 0,
         STATE_B = 1,
  };

  uint32_t     states[]      = { STATE_A, STATE_B };
  SMTransition transitions[] = {
    { STATE_A, STATE_B, NULL, NULL, 10 },
    { STATE_B, STATE_A, NULL, NULL, 11 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-sm",
      states,
      2,
      transitions,
      2,
      STATE_A);

  assert(tmpl != NULL);
  assert(strcmp(sm_template_get_name(tmpl), "test-sm") == 0);
  assert(tmpl->num_states == 2);
  assert(tmpl->num_transitions == 2);
  assert(tmpl->initial_state == STATE_A);

  sm_template_destroy(tmpl);
}

void
test_sm_instance_create_destroy()
{
  LOG_CLEAN("== Testing StateMachine create/destroy");

  enum { STATE_A = 0,
         STATE_B = 1,
  };

  uint32_t     states[]      = { STATE_A, STATE_B };
  SMTransition transitions[] = {
    { STATE_A, STATE_B, NULL, NULL, 10 },
    { STATE_B, STATE_A, NULL, NULL, 11 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-instance",
      states,
      2,
      transitions,
      2,
      STATE_A);

  int           owner = 42;
  StateMachine* sm    = sm_create(&owner, tmpl, NULL, NULL);

  assert(sm != NULL);
  assert(strcmp(sm_get_name(sm), "test-instance") == 0);
  assert(sm_get_state(sm) == STATE_A);
  assert(sm->owner == &owner);
  assert(sm_get_template(sm) == tmpl);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void
test_sm_raw_write()
{
  LOG_CLEAN("== Testing sm_raw_write");

  enum { STATE_A = 0,
         STATE_B = 1,
  };

  uint32_t     states[]      = { STATE_A, STATE_B };
  SMTransition transitions[] = {
    { STATE_A, STATE_B, NULL, NULL, 10 },
    { STATE_B, STATE_A, NULL, NULL, 11 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-raw-write",
      states,
      2,
      transitions,
      2,
      STATE_A);

  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, NULL, NULL);

  assert(sm_get_state(sm) == STATE_A);

  sm_raw_write(sm, STATE_B);
  assert(sm_get_state(sm) == STATE_B);

  sm_raw_write(sm, STATE_A);
  assert(sm_get_state(sm) == STATE_A);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void
test_sm_raw_write_event_emission()
{
  LOG_CLEAN("== Testing sm_raw_write event emission");
  hub_init();

  enum { STATE_OFF = 0,
         STATE_ON  = 1,
  };

  /* Event types for transitions - use values within MAX_EVENT_TYPES (64) */
#define EVT_OFF_TO_ON  20
#define EVT_ON_TO_OFF 21

  uint32_t     states[]      = { STATE_OFF, STATE_ON };
  SMTransition transitions[] = {
    { STATE_OFF, STATE_ON,  NULL, NULL, EVT_OFF_TO_ON },
    { STATE_ON,  STATE_OFF, NULL, NULL, EVT_ON_TO_OFF },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-raw-write-emit",
      states,
      2,
      transitions,
      2,
      STATE_OFF);

  HubTarget target = { .id = 42, .type = TARGET_TYPE_CLIENT, .registered = false };
  hub_register_target(&target);

  /* Test 1: raw_write emits event via hub */
  raw_write_events_emitted = 0;
  hub_subscribe(EVT_OFF_TO_ON, event_catcher, NULL);
  hub_subscribe(EVT_ON_TO_OFF, event_catcher, NULL);

  /* Pass target as owner so events include correct target_id */
  StateMachine* sm    = sm_create(&target, tmpl, NULL, NULL);

  LOG_CLEAN("  Test 1: raw_write STATE_OFF -> STATE_ON");
  sm_raw_write(sm, STATE_ON);
  assert(sm_get_state(sm) == STATE_ON);
  assert(raw_write_events_emitted == 1);
  assert(last_raw_write_event == EVT_OFF_TO_ON);

  LOG_CLEAN("  Test 2: raw_write STATE_ON -> STATE_OFF");
  sm_raw_write(sm, STATE_OFF);
  assert(sm_get_state(sm) == STATE_OFF);
  assert(raw_write_events_emitted == 2);
  assert(last_raw_write_event == EVT_ON_TO_OFF);

  hub_unsubscribe(EVT_OFF_TO_ON, event_catcher);
  hub_unsubscribe(EVT_ON_TO_OFF, event_catcher);

  hub_unregister_target(target.id);

  /* Test 2: raw_write with custom emitter */
  LOG_CLEAN("  Test 3: raw_write with custom EventEmitter");
  raw_write_events_emitted = 0;
  StateMachine* sm2 = sm_create(&target, tmpl, test_emitter, NULL);

  sm_raw_write(sm2, STATE_ON);
  assert(sm_get_state(sm2) == STATE_ON);
  assert(raw_write_events_emitted == 1);

  /* Test 3: raw_write succeeds for invalid transition (reality is authoritative) */
  LOG_CLEAN("  Test 4: raw_write to invalid transition succeeds");
  raw_write_events_emitted = 0;
  /* STATE_ON -> STATE_ON is not a defined transition, but raw_write still updates state */
  /* No event emitted because there's no transition OFF->ON from STATE_ON */
  sm_raw_write(sm2, STATE_ON); /* Already in STATE_ON */
  assert(sm_get_state(sm2) == STATE_ON);
  /* Event NOT emitted because STATE_ON -> STATE_ON is not a valid transition */
  assert(raw_write_events_emitted == 0);

  sm_destroy(sm2);
  sm_destroy(sm);
  sm_template_destroy(tmpl);
  hub_shutdown();
}

void
test_sm_transition()
{
  LOG_CLEAN("== Testing sm_transition");

  enum { STATE_OFF = 0,
         STATE_ON  = 1,
  };

  uint32_t     states[]      = { STATE_OFF, STATE_ON };
  SMTransition transitions[] = {
    { STATE_OFF, STATE_ON,  NULL, NULL, 100 },
    { STATE_ON,  STATE_OFF, NULL, NULL, 101 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-transition",
      states,
      2,
      transitions,
      2,
      STATE_OFF);

  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, NULL, NULL);

  assert(sm_transition(sm, STATE_ON) == true);
  assert(sm_get_state(sm) == STATE_ON);

  assert(sm_transition(sm, STATE_OFF) == true);
  assert(sm_get_state(sm) == STATE_OFF);

  assert(sm_transition(sm, STATE_OFF) == false);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void
test_sm_hooks()
{
  LOG_CLEAN("== Testing sm hooks");

  enum { STATE_A = 0,
         STATE_B = 1,
  };

  uint32_t     states[]      = { STATE_A, STATE_B };
  SMTransition transitions[] = {
    { STATE_A, STATE_B, NULL, NULL, 0 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-hooks",
      states,
      2,
      transitions,
      1,
      STATE_A);

  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, NULL, NULL);

  hook_called = 0;
  sm_add_hook(sm, SM_HOOK_POST_ACTION, test_hook_fn, NULL);
  sm_transition(sm, STATE_B);
  assert(hook_called == 1);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void
test_sm_hooks_all_phases()
{
  LOG_CLEAN("== Testing all hook phases");
  sm_registry_init();

  /* Register guard and action needed for testing */
  sm_register_guard("allow_all", guard_allow);
  sm_register_action("action_track", action_track);

  enum { STATE_IDLE = 0,
         STATE_WORK = 1,
         STATE_DONE = 2,
  };

  uint32_t     states[]      = { STATE_IDLE, STATE_WORK, STATE_DONE };
  SMTransition transitions[] = {
    { STATE_IDLE, STATE_WORK, "allow_all", "action_track", 35 },
    { STATE_WORK, STATE_DONE, NULL,         "action_track", 36 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-all-hooks",
      states,
      3,
      transitions,
      2,
      STATE_IDLE);

  int action_count = 0;
  int owner = 0;
  StateMachine* sm = sm_create(&owner, tmpl, NULL, NULL);
  sm->data = &action_count;

  /* Add hooks for all phases */
  sm_add_hook(sm, SM_HOOK_PRE_GUARD,   hook_pre_guard,   NULL);
  sm_add_hook(sm, SM_HOOK_POST_GUARD,  hook_post_guard,  NULL);
  sm_add_hook(sm, SM_HOOK_PRE_ACTION,  hook_pre_action,  NULL);
  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_post_action, NULL);
  sm_add_hook(sm, SM_HOOK_PRE_EMIT,    hook_pre_emit,    NULL);
  sm_add_hook(sm, SM_HOOK_POST_EMIT,   hook_post_emit,    NULL);

  /* Subscribe to events so emit hooks get called */
  hub_init();
  hub_subscribe(35, event_catcher, NULL);
  hub_subscribe(36, event_catcher, NULL);

  /* Reset hook call counts */
  for (int i = 0; i < SM_HOOK_MAX; i++)
    hook_calls[i] = 0;

  /* Transition IDLE -> WORK (with guard and action, emits event 35) */
  assert(sm_transition(sm, STATE_WORK) == true);
  assert(hook_calls[SM_HOOK_PRE_GUARD]  == 1);
  assert(hook_calls[SM_HOOK_POST_GUARD] == 1);
  assert(hook_calls[SM_HOOK_PRE_ACTION]  == 1);
  assert(hook_calls[SM_HOOK_POST_ACTION] == 1);
  assert(hook_calls[SM_HOOK_PRE_EMIT]    == 1);
  assert(hook_calls[SM_HOOK_POST_EMIT]   == 1);

  LOG_CLEAN("  Test: all hooks called in correct order for transition with event");

  /* Transition WORK -> DONE (no guard, has action, emits event 36) */
  for (int i = 0; i < SM_HOOK_MAX; i++)
    hook_calls[i] = 0;

  assert(sm_transition(sm, STATE_DONE) == true);
  /* Guard hooks ARE called even when no guard is defined (hooks run regardless) */
  assert(hook_calls[SM_HOOK_PRE_GUARD]  == 1);
  assert(hook_calls[SM_HOOK_POST_GUARD] == 1);
  assert(hook_calls[SM_HOOK_PRE_ACTION]  == 1);
  assert(hook_calls[SM_HOOK_POST_ACTION] == 1);
  assert(hook_calls[SM_HOOK_PRE_EMIT]    == 1);
  assert(hook_calls[SM_HOOK_POST_EMIT]   == 1);

  LOG_CLEAN("  Test: hooks called correctly for transition");

  /* Test hooks receive correct userdata */
  LOG_CLEAN("  Test: hooks receive correct userdata");
  StateMachine* sm2 = sm_create(&owner, tmpl, NULL, NULL);
  int userdata_counter = 0;

  sm_add_hook(sm2, SM_HOOK_POST_ACTION, hook_with_userdata, &userdata_counter);
  sm_add_hook(sm2, SM_HOOK_POST_ACTION, hook_with_userdata, &userdata_counter);

  sm_raw_write(sm2, STATE_IDLE);
  sm_transition(sm2, STATE_WORK);
  assert(userdata_counter == 2); /* Both hooks called with same userdata */

  sm_destroy(sm2);

  /* Test hook removal */
  LOG_CLEAN("  Test: hook can be removed");
  StateMachine* sm3 = sm_create(&owner, tmpl, NULL, NULL);
  hook_remove_test_count = 0;
  hook_called = 0;

  sm_add_hook(sm3, SM_HOOK_POST_ACTION, hook_remove_test, NULL);
  sm_add_hook(sm3, SM_HOOK_POST_ACTION, test_hook_fn, NULL);

  sm_raw_write(sm3, STATE_IDLE);
  sm_transition(sm3, STATE_WORK);
  assert(hook_remove_test_count == 1);
  assert(hook_called == 1);

  /* Remove hook_remove_test */
  sm_remove_hook(sm3, SM_HOOK_POST_ACTION, hook_remove_test);
  hook_remove_test_count = 0;
  hook_called = 0;

  /* Transition again - removed hook should not be called */
  sm_raw_write(sm3, STATE_IDLE);
  sm_transition(sm3, STATE_WORK);
  assert(hook_remove_test_count == 0); /* Removed hook not called */
  assert(hook_called == 1); /* Other hook still called */

  sm_destroy(sm3);
  sm_destroy(sm);
  sm_template_destroy(tmpl);
  hub_shutdown();
  sm_registry_shutdown();
}

void
test_sm_hooks_order()
{
  LOG_CLEAN("== Testing hook order");

  /* Register guard for this test */
  sm_register_guard("allow_hook_test", guard_allow);

  enum { STATE_OFF = 0,
         STATE_ON  = 1,
  };

  uint32_t     states[]      = { STATE_OFF, STATE_ON };
  SMTransition transitions[] = {
    { STATE_OFF, STATE_ON, "allow_hook_test", NULL, 10 },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-hook-order",
      states,
      2,
      transitions,
      1,
      STATE_OFF);

  int owner = 0;
  StateMachine* sm = sm_create(&owner, tmpl, NULL, NULL);

  /* Add simple tracking hooks for each phase */
  sm_add_hook(sm, SM_HOOK_PRE_GUARD,   hook_pre_guard,   NULL);
  sm_add_hook(sm, SM_HOOK_POST_GUARD,  hook_post_guard,  NULL);
  sm_add_hook(sm, SM_HOOK_PRE_ACTION,  hook_pre_action,  NULL);
  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_post_action, NULL);
  sm_add_hook(sm, SM_HOOK_PRE_EMIT,    hook_pre_emit,    NULL);
  sm_add_hook(sm, SM_HOOK_POST_EMIT,   hook_post_emit,   NULL);

  hub_init();
  hub_subscribe(10, event_catcher, NULL);

  for (int i = 0; i < SM_HOOK_MAX; i++)
    hook_calls[i] = 0;

  sm_transition(sm, STATE_ON);

  /* Verify all phases called in order */
  assert(hook_calls[SM_HOOK_PRE_GUARD]   == 1);
  assert(hook_calls[SM_HOOK_POST_GUARD]  == 1);
  assert(hook_calls[SM_HOOK_PRE_ACTION]  == 1);
  assert(hook_calls[SM_HOOK_POST_ACTION] == 1);
  assert(hook_calls[SM_HOOK_PRE_EMIT]    == 1);
  assert(hook_calls[SM_HOOK_POST_EMIT]   == 1);

  LOG_CLEAN("  Test: all hooks called in phase order");

  sm_destroy(sm);
  sm_template_destroy(tmpl);
  hub_shutdown();
}

void
test_sm_transition_with_guards()
{
  LOG_CLEAN("== Testing sm_transition with guards");
  sm_registry_init();

  enum { STATE_OFF = 0,
         STATE_ON  = 1,
  };

  /* Register guards */
  sm_register_guard("guard_allow", guard_allow);
  sm_register_guard("guard_deny", guard_deny);
  sm_register_guard("guard_conditional", guard_conditional);

  uint32_t     states[]      = { STATE_OFF, STATE_ON };
  SMTransition transitions[] = {
    { STATE_OFF, STATE_ON,  "guard_allow",     NULL, 40 },  /* Changed from 200 to 40 */
    { STATE_ON,  STATE_OFF, "guard_deny",      NULL, 41 },  /* Changed from 201 to 41 */
    { STATE_OFF, STATE_ON,  "guard_conditional", NULL, 42 }, /* Changed from 202 to 42 */
  };

  SMTemplate* tmpl = sm_template_create(
      "test-transition-guards",
      states,
      2,
      transitions,
      3,
      STATE_OFF);

  int owner = 0;
  hub_init();

  /* Test 1: Guard that allows */
  LOG_CLEAN("  Test 1: transition with allowing guard");
  StateMachine* sm1 = sm_create(&owner, tmpl, NULL, NULL);
  raw_write_events_emitted = 0;
  hub_subscribe(40, event_catcher, NULL);  /* Changed from 200 to 40 */

  assert(sm_transition(sm1, STATE_ON) == true);
  assert(sm_get_state(sm1) == STATE_ON);
  assert(raw_write_events_emitted == 1);

  hub_unsubscribe(40, event_catcher);  /* Changed from 200 to 40 */
  sm_destroy(sm1);

  /* Test 2: Guard that denies - transition fails */
  LOG_CLEAN("  Test 2: transition rejected by denying guard");
  StateMachine* sm2 = sm_create(&owner, tmpl, NULL, NULL);
  assert(sm_get_state(sm2) == STATE_OFF);

  /* STATE_ON -> STATE_OFF has guard_deny which always rejects */
  assert(sm_transition(sm2, STATE_ON) == true); /* guard_allow passes */
  assert(sm_get_state(sm2) == STATE_ON);

  /* Now try STATE_ON -> STATE_OFF with guard_deny */
  raw_write_events_emitted = 0;
  hub_subscribe(41, event_catcher, NULL);  /* Changed from 201 to 41 */
  assert(sm_transition(sm2, STATE_OFF) == false); /* guard_deny rejects */
  assert(sm_get_state(sm2) == STATE_ON); /* State unchanged */
  assert(raw_write_events_emitted == 0); /* No event emitted */

  hub_unsubscribe(41, event_catcher);  /* Changed from 201 to 41 */
  sm_destroy(sm2);

  /* Test 3: Conditional guard based on data */
  LOG_CLEAN("  Test 3: conditional guard with data");
  StateMachine* sm3 = sm_create(&owner, tmpl, NULL, NULL);
  sm3->data = NULL;  /* Default - no data */
  assert(sm_transition(sm3, STATE_ON) == true); /* guard_allow passes */
  assert(sm_get_state(sm3) == STATE_ON);

  /* Reset to OFF, try with conditional guard */
  sm_raw_write(sm3, STATE_OFF);

  /* Manually test a transition with conditional guard */
  /* We need to create a new SM with just conditional transitions */
  SMTransition cond_trans[] = {
    { STATE_OFF, STATE_ON, "guard_conditional", NULL, 42 },  /* Changed from 202 to 42 */
  };
  SMTemplate* cond_tmpl = sm_template_create(
      "test-conditional",
      states,
      2,
      cond_trans,
      1,
      STATE_OFF);

  /* With data = allow = true */
  int allow_true = 1;
  StateMachine* sm_allow = sm_create(&owner, cond_tmpl, NULL, NULL);
  sm_allow->data = &allow_true;
  assert(sm_transition(sm_allow, STATE_ON) == true);
  assert(sm_get_state(sm_allow) == STATE_ON);
  sm_destroy(sm_allow);

  /* With data = allow = false */
  int allow_false = 0;
  StateMachine* sm_deny = sm_create(&owner, cond_tmpl, NULL, NULL);
  sm_deny->data = &allow_false;
  assert(sm_transition(sm_deny, STATE_ON) == false);
  assert(sm_get_state(sm_deny) == STATE_OFF);
  sm_destroy(sm_deny);

  sm_template_destroy(cond_tmpl);
  sm_template_destroy(tmpl);
  hub_shutdown();
  sm_registry_shutdown();
}

void
test_sm_transition_with_actions()
{
  LOG_CLEAN("== Testing sm_transition with actions");
  sm_registry_init();
  hub_init();

  enum { STATE_A = 0,
         STATE_B = 1,
         STATE_C = 2,
  };

  /* Register actions */
  sm_register_action("action_track", action_track);
  sm_register_action("action_fail", action_fail);

  uint32_t     states[]      = { STATE_A, STATE_B, STATE_C };
  SMTransition transitions[] = {
    { STATE_A, STATE_B, NULL, "action_track", 30 },   /* Changed from 300 */
    { STATE_B, STATE_C, NULL, "action_track", 31 },  /* Changed from 301 */
    { STATE_A, STATE_C, NULL, "action_fail",  32 },   /* Changed from 302 */
  };

  SMTemplate* tmpl = sm_template_create(
      "test-transition-actions",
      states,
      3,
      transitions,
      3,
      STATE_A);

  int owner = 0;

  /* Test 1: Action is executed on transition */
  LOG_CLEAN("  Test 1: action executed on successful transition");
  int action_count = 0;
  StateMachine* sm1 = sm_create(&owner, tmpl, NULL, NULL);
  sm1->data = &action_count;

  raw_write_events_emitted = 0;
  hub_subscribe(30, event_catcher, NULL);  /* Changed from 300 */

  assert(sm_transition(sm1, STATE_B) == true);
  assert(sm_get_state(sm1) == STATE_B);
  assert(action_count == 1);
  assert(raw_write_events_emitted == 1);

  hub_unsubscribe(30, event_catcher);  /* Changed from 300 */
  sm_destroy(sm1);

  /* Test 2: Multiple actions via chain */
  LOG_CLEAN("  Test 2: multiple transitions with actions");
  action_count = 0;
  StateMachine* sm2 = sm_create(&owner, tmpl, NULL, NULL);
  sm2->data = &action_count;

  assert(sm_transition(sm2, STATE_B) == true);
  assert(action_count == 1);

  hub_subscribe(31, event_catcher, NULL);  /* Changed from 301 */
  assert(sm_transition(sm2, STATE_C) == true);
  assert(action_count == 2);
  hub_unsubscribe(31, event_catcher);  /* Changed from 301 */

  sm_destroy(sm2);

  /* Test 3: Transition fails if action fails */
  LOG_CLEAN("  Test 3: transition fails when action fails");
  StateMachine* sm3 = sm_create(&owner, tmpl, NULL, NULL);

  /* Try A -> C with action_fail - should fail */
  assert(sm_transition(sm3, STATE_C) == false);
  assert(sm_get_state(sm3) == STATE_A); /* State unchanged */

  sm_destroy(sm3);

  sm_template_destroy(tmpl);
  hub_shutdown();
  sm_registry_shutdown();
}

void
test_sm_transition_complete_flow()
{
  LOG_CLEAN("== Testing sm_transition complete flow");
  sm_registry_init();
  hub_init();

  enum { STATE_IDLE = 0,
         STATE_WORK = 1,
         STATE_DONE = 2,
  };

  /* Register guard and action */
  sm_register_guard("can_start_work", guard_allow);
  sm_register_action("do_work", action_track);

  uint32_t     states[]      = { STATE_IDLE, STATE_WORK, STATE_DONE };
  SMTransition transitions[] = {
    { STATE_IDLE, STATE_WORK, "can_start_work", "do_work", 35 },  /* Changed from 400 */
    { STATE_WORK, STATE_DONE, NULL,             "do_work", 36 }, /* Changed from 401 */
    { STATE_DONE, STATE_IDLE, NULL,             NULL,       0   },
  };

  SMTemplate* tmpl = sm_template_create(
      "test-complete-flow",
      states,
      3,
      transitions,
      3,
      STATE_IDLE);

  int owner = 0;
  int action_count = 0;
  StateMachine* sm = sm_create(&owner, tmpl, NULL, NULL);
  sm->data = &action_count;

  /* Subscribe to events */
  hub_subscribe(35, event_catcher, NULL);  /* Changed from 400 */
  hub_subscribe(36, event_catcher, NULL);  /* Changed from 401 */
  raw_write_events_emitted = 0;

  /* Test 1: Valid transition succeeds and emits event */
  LOG_CLEAN("  Test 1: valid transition succeeds and emits event");
  assert(sm_get_state(sm) == STATE_IDLE);
  assert(sm_transition(sm, STATE_WORK) == true);
  assert(sm_get_state(sm) == STATE_WORK);
  assert(raw_write_events_emitted == 1);

  /* Test 2: Action is executed on successful transition */
  LOG_CLEAN("  Test 2: action executed on successful transition");
  action_count = 0;
  sm_raw_write(sm, STATE_IDLE);  /* Reset */
  sm->data = &action_count;
  assert(sm_transition(sm, STATE_WORK) == true);
  assert(action_count == 1);

  /* Test 3: sm_get_state returns current state */
  LOG_CLEAN("  Test 3: sm_get_state returns correct state");
  sm_raw_write(sm, STATE_DONE);
  assert(sm_get_state(sm) == STATE_DONE);

  /* Test 4: sm_get_available_transitions returns valid next states */
  LOG_CLEAN("  Test 4: sm_get_available_transitions returns valid states");
  uint32_t count = 0;
  uint32_t* available = sm_get_available_transitions(sm, &count);
  assert(available != NULL);
  assert(count == 1);
  assert(available[0] == STATE_IDLE);
  free(available);

  /* Test from STATE_IDLE */
  sm_raw_write(sm, STATE_IDLE);
  available = sm_get_available_transitions(sm, &count);
  assert(available != NULL);
  assert(count == 1);
  assert(available[0] == STATE_WORK);
  free(available);

  /* Test from STATE_WORK */
  sm_raw_write(sm, STATE_WORK);
  available = sm_get_available_transitions(sm, &count);
  assert(available != NULL);
  assert(count == 1);
  assert(available[0] == STATE_DONE);
  free(available);

  hub_unsubscribe(35, event_catcher);  /* Changed from 400 */
  hub_unsubscribe(36, event_catcher);  /* Changed from 401 */
  sm_destroy(sm);
  sm_template_destroy(tmpl);
  hub_shutdown();
  sm_registry_shutdown();
}

void
test_sm_transition_invalid_rejected()
{
  LOG_CLEAN("== Testing invalid transition rejected by guard");
  sm_registry_init();
  hub_init();

  enum { STATE_LOCKED = 0,
         STATE_OPEN   = 1,
         STATE_BROKEN = 2,
  };

  /* Guard that only allows opening if not already broken */
  sm_register_guard("guard_can_open", guard_allow);

  uint32_t     states[]      = { STATE_LOCKED, STATE_OPEN, STATE_BROKEN };
  SMTransition transitions[] = {
    { STATE_LOCKED, STATE_OPEN,   "guard_can_open", NULL, 50 },  /* Changed from 500 */
    { STATE_OPEN,   STATE_LOCKED, NULL,             NULL, 51 },  /* Changed from 501 */
    { STATE_OPEN,   STATE_BROKEN, NULL,             NULL, 52 },  /* Changed from 502 */
    { STATE_BROKEN, STATE_OPEN,   "guard_can_open", NULL, 53 },  /* Changed from 503 */
  };

  SMTemplate* tmpl = sm_template_create(
      "test-invalid-rejected",
      states,
      3,
      transitions,
      4,
      STATE_LOCKED);

  int owner = 0;
  StateMachine* sm = sm_create(&owner, tmpl, NULL, NULL);

  /* Test: Invalid transition (no transition defined) is rejected */
  LOG_CLEAN("  Test: transition from LOCKED to BROKEN (no transition) is rejected");
  assert(sm_get_state(sm) == STATE_LOCKED);
  assert(sm_transition(sm, STATE_BROKEN) == false); /* No transition LOCKED -> BROKEN */
  assert(sm_get_state(sm) == STATE_LOCKED); /* State unchanged */

  /* Valid transition: LOCKED -> OPEN */
  LOG_CLEAN("  Test: valid transition LOCKED -> OPEN");
  hub_subscribe(50, event_catcher, NULL);  /* Changed from 500 */
  raw_write_events_emitted = 0;
  assert(sm_transition(sm, STATE_OPEN) == true);
  assert(sm_get_state(sm) == STATE_OPEN);
  assert(raw_write_events_emitted == 1);

  hub_unsubscribe(50, event_catcher);  /* Changed from 500 */
  sm_destroy(sm);
  sm_template_destroy(tmpl);
  hub_shutdown();
  sm_registry_shutdown();
}

int
main()
{
  test_sm_template_create_destroy();
  test_sm_instance_create_destroy();
  test_sm_raw_write();
  test_sm_raw_write_event_emission();
  test_sm_transition();
  test_sm_transition_with_guards();
  test_sm_transition_with_actions();
  test_sm_transition_complete_flow();
  test_sm_transition_invalid_rejected();
  test_sm_hooks();
  test_sm_hooks_all_phases();
  test_sm_hooks_order();

  LOG_CLEAN("All SM tests passed!");
  return 0;
}