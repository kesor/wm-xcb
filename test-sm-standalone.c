#define DEBUG 1

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock running for LOG_FATAL */
sig_atomic_t running = 1;

/* Minimal LOG mocks */
#define LOG_DEBUG(pFormat, ...)                    \
  {                                                \
    printf("DEBUG: " pFormat "\n", ##__VA_ARGS__); \
  }

#define LOG_CLEAN(pFormat, ...)          \
  {                                      \
    printf(pFormat "\n", ##__VA_ARGS__); \
  }

#define LOG_ERROR(pFormat, ...)                             \
  {                                                        \
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

/* ==================== TEST FIXTURE ==================== */

static int g_raw_write_events = 0;
static uint32_t g_last_event = 0;

static void event_catcher(Event e) {
  g_raw_write_events++;
  g_last_event = e.type;
  LOG_DEBUG("Event caught: type=%u", e.type);
}

static void test_emitter(StateMachine* sm, uint32_t from, uint32_t to, void* ud) {
  (void)sm; (void)from; (void)to; (void)ud;
  LOG_DEBUG("Custom emitter called");
  g_raw_write_events++;
}

/* Guard / Action helpers */
static bool guard_allow(StateMachine* sm, void* data) {
  (void)sm; (void)data;
  return true;
}

static bool guard_deny(StateMachine* sm, void* data) {
  (void)sm; (void)data;
  return false;
}

static bool guard_conditional(StateMachine* sm, void* data) {
  (void)sm;
  int* allow = (int*)data;
  return allow && *allow;
}

static bool action_fail(StateMachine* sm, void* data) {
  (void)sm; (void)data;
  return false;
}

static bool action_track(StateMachine* sm, void* data) {
  (void)sm;
  int* count = (int*)data;
  if (count) (*count)++;
  return true;
}

/* Hook tracking */
static int g_hook_calls[SM_HOOK_MAX];
static int g_hook_order[SM_HOOK_MAX * 2];
static int g_hook_order_count;

static void reset_hook_tracking(void) {
  memset(g_hook_calls, 0, sizeof(g_hook_calls));
  g_hook_order_count = 0;
}

static void hook_pre_guard(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_calls[SM_HOOK_PRE_GUARD]++;
  g_hook_order[g_hook_order_count++] = SM_HOOK_PRE_GUARD;
}

static void hook_post_guard(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_calls[SM_HOOK_POST_GUARD]++;
  g_hook_order[g_hook_order_count++] = SM_HOOK_POST_GUARD;
}

static void hook_pre_action(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_calls[SM_HOOK_PRE_ACTION]++;
  g_hook_order[g_hook_order_count++] = SM_HOOK_PRE_ACTION;
}

static void hook_post_action(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_calls[SM_HOOK_POST_ACTION]++;
  g_hook_order[g_hook_order_count++] = SM_HOOK_POST_ACTION;
}

static void hook_pre_emit(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_calls[SM_HOOK_PRE_EMIT]++;
  g_hook_order[g_hook_order_count++] = SM_HOOK_PRE_EMIT;
}

static void hook_post_emit(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_calls[SM_HOOK_POST_EMIT]++;
  g_hook_order[g_hook_order_count++] = SM_HOOK_POST_EMIT;
}

static void hook_with_userdata(StateMachine* sm, void* userdata) {
  (void)sm;
  int* counter = (int*)userdata;
  if (counter) (*counter)++;
}

static int g_hook_remove_count = 0;
static void hook_remove_test(StateMachine* sm, void* ud) {
  (void)sm; (void)ud;
  g_hook_remove_count++;
}

/* ==================== TEST HELPERS ==================== */

/* Simple state enum for most tests */
enum { S0 = 0, S1 = 1, S2 = 2 };

static SMTemplate* make_tmpl(const char* name, uint32_t* states, int nstates,
                             SMTransition* trans, int ntrans, uint32_t init) {
  return sm_template_create(name, states, nstates, trans, ntrans, init);
}

/* Create SM with custom emitter (no hub needed) */
static StateMachine* make_sm(void* owner, SMTemplate* tmpl) {
  return sm_create(owner, tmpl, test_emitter, NULL);
}

/* ==================== TESTS ==================== */

void test_tmpl_create_destroy(void) {
  LOG_CLEAN("== SMTemplate create/destroy");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 10 }, { S1, S0, NULL, NULL, 11 } };

  SMTemplate* t = make_tmpl("test", states, 2, trans, 2, S0);
  assert(t != NULL);
  assert(strcmp(sm_template_get_name(t), "test") == 0);
  assert(t->num_states == 2);
  assert(t->initial_state == S0);
  sm_template_destroy(t);
}

void test_sm_create_destroy(void) {
  LOG_CLEAN("== StateMachine create/destroy");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 10 } };

  SMTemplate* t = make_tmpl("test", states, 2, trans, 1, S0);
  int owner = 42;
  StateMachine* sm = make_sm(&owner, t);

  assert(sm != NULL);
  assert(sm_get_state(sm) == S0);
  assert(sm->owner == &owner);
  assert(strcmp(sm_get_name(sm), "test") == 0);
  assert(sm_get_template(sm) == t);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_sm_create_requires_params(void) {
  LOG_CLEAN("== sm_create requires owner and template");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 10 } };
  SMTemplate* t = make_tmpl("test", states, 2, trans, 1, S0);

  /* NULL owner fails */
  assert(sm_create(NULL, t, test_emitter, NULL) == NULL);

  /* NULL template fails */
  int owner = 0;
  assert(sm_create(&owner, NULL, test_emitter, NULL) == NULL);

  sm_template_destroy(t);
}

void test_sm_set_emitter(void) {
  LOG_CLEAN("== sm_set_emitter after creation");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 10 }, { S1, S0, NULL, NULL, 11 } };
  SMTemplate* t = make_tmpl("test", states, 2, trans, 2, S0);

  int owner = 0;
  StateMachine* sm = sm_create(&owner, t, NULL, NULL);
  assert(sm != NULL);
  assert(sm->emit == NULL);

  /* Set emitter after creation */
  sm_set_emitter(sm, test_emitter, NULL);
  assert(sm->emit == test_emitter);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_raw_write(void) {
  LOG_CLEAN("== sm_raw_write");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 10 }, { S1, S0, NULL, NULL, 11 } };
  SMTemplate* t = make_tmpl("raw", states, 2, trans, 2, S0);

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  assert(sm_get_state(sm) == S0);
  sm_raw_write(sm, S1);
  assert(sm_get_state(sm) == S1);
  sm_raw_write(sm, S0);
  assert(sm_get_state(sm) == S0);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_raw_write_emits_via_hub(void) {
  LOG_CLEAN("== sm_raw_write emits via hub");
  hub_init();

  enum { OFF = 20, ON = 21 };
  uint32_t states[] = { OFF, ON };
  SMTransition trans[] = { { OFF, ON, NULL, NULL, OFF }, { ON, OFF, NULL, NULL, ON } };
  SMTemplate* t = make_tmpl("raw-emit", states, 2, trans, 2, OFF);

  HubTarget target = { .id = 42, .type = TARGET_TYPE_CLIENT, .registered = false };
  hub_register_target(&target);

  hub_subscribe(OFF, event_catcher, NULL);
  hub_subscribe(ON, event_catcher, NULL);

  g_raw_write_events = 0;
  StateMachine* sm = sm_create(&target, t, NULL, NULL);

  sm_raw_write(sm, ON);
  assert(sm_get_state(sm) == ON);
  assert(g_raw_write_events == 1);
  assert(g_last_event == OFF);

  sm_raw_write(sm, OFF);
  assert(sm_get_state(sm) == OFF);
  assert(g_raw_write_events == 2);
  assert(g_last_event == ON);

  hub_unsubscribe(OFF, event_catcher);
  hub_unsubscribe(ON, event_catcher);
  hub_unregister_target(target.id);

  sm_destroy(sm);
  sm_template_destroy(t);
  hub_shutdown();
}

void test_raw_write_with_custom_emitter(void) {
  LOG_CLEAN("== sm_raw_write with custom emitter");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 10 }, { S1, S0, NULL, NULL, 11 } };
  SMTemplate* t = make_tmpl("custom-emit", states, 2, trans, 2, S0);

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  g_raw_write_events = 0;
  sm_raw_write(sm, S1);
  assert(g_raw_write_events == 1);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_transition(void) {
  LOG_CLEAN("== sm_transition");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 100 }, { S1, S0, NULL, NULL, 101 } };
  SMTemplate* t = make_tmpl("trans", states, 2, trans, 2, S0);

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  assert(sm_transition(sm, S1) == true);
  assert(sm_get_state(sm) == S1);

  assert(sm_transition(sm, S0) == true);
  assert(sm_get_state(sm) == S0);

  /* Invalid transition: no path from S0 to S0 */
  assert(sm_transition(sm, S0) == false);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_can_transition(void) {
  LOG_CLEAN("== sm_can_transition");
  uint32_t states[] = { S0, S1, S2 };
  SMTransition trans[] = {
    { S0, S1, NULL, NULL, 0 },
    { S1, S2, NULL, NULL, 0 },
  };
  SMTemplate* t = make_tmpl("can-trans", states, 3, trans, 2, S0);

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  /* From S0: can go to S1, but not to S2 */
  assert(sm_can_transition(sm, S1) == true);
  assert(sm_can_transition(sm, S2) == false);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_transition_with_guards(void) {
  LOG_CLEAN("== sm_transition with guards");
  sm_registry_init();
  sm_register_guard("allow", guard_allow);
  sm_register_guard("deny", guard_deny);
  sm_register_guard("cond", guard_conditional);

  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = {
    { S0, S1, "allow", NULL, 40 },
    { S1, S0, "deny",  NULL, 41 },
  };
  SMTemplate* t = make_tmpl("guards", states, 2, trans, 2, S0);

  int owner = 0;

  /* Test 1: allowing guard */
  g_raw_write_events = 0;
  StateMachine* sm1 = make_sm(&owner, t);
  assert(sm_transition(sm1, S1) == true);
  assert(sm_get_state(sm1) == S1);
  sm_destroy(sm1);

  /* Test 2: denying guard blocks transition */
  StateMachine* sm2 = make_sm(&owner, t);
  assert(sm_transition(sm2, S1) == true); /* S0 -> S1 with "allow" */
  g_raw_write_events = 0;
  assert(sm_transition(sm2, S0) == false); /* blocked by "deny" */
  assert(sm_get_state(sm2) == S1);
  assert(g_raw_write_events == 0); /* no event emitted */
  sm_destroy(sm2);

  /* Test 3: conditional guard based on data */
  SMTransition ctrans[] = { { S0, S1, "cond", NULL, 42 } };
  SMTemplate* ct = make_tmpl("cond", states, 2, ctrans, 1, S0);

  int allow_true = 1;
  StateMachine* sm_allow = make_sm(&owner, ct);
  sm_allow->data = &allow_true;
  assert(sm_transition(sm_allow, S1) == true);
  sm_destroy(sm_allow);

  int allow_false = 0;
  StateMachine* sm_deny = make_sm(&owner, ct);
  sm_deny->data = &allow_false;
  assert(sm_transition(sm_deny, S1) == false);
  sm_destroy(sm_deny);

  sm_template_destroy(ct);
  sm_template_destroy(t);
  sm_registry_shutdown();
}

void test_transition_with_actions(void) {
  LOG_CLEAN("== sm_transition with actions");
  sm_registry_init();
  sm_register_action("track", action_track);
  sm_register_action("fail", action_fail);

  uint32_t states[] = { S0, S1, S2 };
  SMTransition trans[] = {
    { S0, S1, NULL, "track", 30 },
    { S1, S2, NULL, "track", 31 },
    { S0, S2, NULL, "fail",  32 },
  };
  SMTemplate* t = make_tmpl("actions", states, 3, trans, 3, S0);

  int owner = 0;

  /* Action runs on successful transition */
  int count = 0;
  StateMachine* sm1 = make_sm(&owner, t);
  sm1->data = &count;
  g_raw_write_events = 0;

  assert(sm_transition(sm1, S1) == true);
  assert(count == 1);
  assert(g_raw_write_events == 1);
  sm_destroy(sm1);

  /* Chain of actions */
  count = 0;
  StateMachine* sm2 = make_sm(&owner, t);
  sm2->data = &count;
  assert(sm_transition(sm2, S1) == true);
  assert(sm_transition(sm2, S2) == true);
  assert(count == 2);
  sm_destroy(sm2);

  /* Failed action prevents transition */
  StateMachine* sm3 = make_sm(&owner, t);
  assert(sm_transition(sm3, S2) == false);
  assert(sm_get_state(sm3) == S0); /* unchanged */
  sm_destroy(sm3);

  sm_template_destroy(t);
  sm_registry_shutdown();
}

void test_complete_flow(void) {
  LOG_CLEAN("== sm_transition complete flow");
  sm_registry_init();
  sm_register_guard("can_work", guard_allow);
  sm_register_action("do_work", action_track);

  uint32_t states[] = { S0, S1, S2 };
  SMTransition trans[] = {
    { S0, S1, "can_work", "do_work", 35 },
    { S1, S2, NULL,       "do_work", 36 },
  };
  SMTemplate* t = make_tmpl("flow", states, 3, trans, 2, S0);

  int owner = 0;
  int count = 0;
  StateMachine* sm = make_sm(&owner, t);
  sm->data = &count;

  /* Valid transition with guard + action */
  g_raw_write_events = 0;
  assert(sm_transition(sm, S1) == true);
  assert(sm_get_state(sm) == S1);
  assert(g_raw_write_events == 1);
  assert(count == 1);

  /* Transition without guard */
  count = 0;
  sm_raw_write(sm, S0);
  sm->data = &count;
  assert(sm_transition(sm, S1) == true);
  assert(count == 1);

  /* get_available_transitions */
  sm_raw_write(sm, S0);
  uint32_t n = 0;
  uint32_t* avail = sm_get_available_transitions(sm, &n);
  assert(avail != NULL);
  assert(n == 1);
  assert(avail[0] == S1);
  free(avail);

  sm_destroy(sm);
  sm_template_destroy(t);
  sm_registry_shutdown();
}

void test_invalid_transition_rejected(void) {
  LOG_CLEAN("== invalid transition rejected");
  sm_registry_init();
  sm_register_guard("can_open", guard_allow);

  enum { LOCKED = 0, OPEN = 1, BROKEN = 2 };
  uint32_t states[] = { LOCKED, OPEN, BROKEN };
  SMTransition trans[] = {
    { LOCKED, OPEN,   "can_open", NULL, 50 },
    { OPEN,   LOCKED, NULL,       NULL, 51 },
    { OPEN,   BROKEN, NULL,       NULL, 52 },
    { BROKEN, OPEN,   "can_open", NULL, 53 },
  };
  SMTemplate* t = make_tmpl("invalid", states, 3, trans, 4, LOCKED);

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  /* No path LOCKED -> BROKEN */
  assert(sm_transition(sm, BROKEN) == false);
  assert(sm_get_state(sm) == LOCKED);

  /* Valid transition works */
  g_raw_write_events = 0;
  assert(sm_transition(sm, OPEN) == true);
  assert(sm_get_state(sm) == OPEN);
  assert(g_raw_write_events == 1);

  sm_destroy(sm);
  sm_template_destroy(t);
  sm_registry_shutdown();
}

void test_hooks_basic(void) {
  LOG_CLEAN("== sm hooks basic");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 0 } };
  SMTemplate* t = make_tmpl("hooks", states, 2, trans, 1, S0);

  int called = 0;
  void hook_fn(StateMachine* sm, void* ud) { (void)sm; (void)ud; called++; }

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);
  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_fn, NULL);
  sm_transition(sm, S1);
  assert(called == 1);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_hooks_all_phases(void) {
  LOG_CLEAN("== sm hooks all phases");
  sm_registry_init();
  sm_register_guard("allow_all", guard_allow);
  sm_register_action("track", action_track);

  enum { IDLE = 0, WORK = 1, DONE = 2 };
  uint32_t states[] = { IDLE, WORK, DONE };
  SMTransition trans[] = {
    { IDLE, WORK, "allow_all", "track", 35 },
    { WORK, DONE, NULL,        "track", 36 },
  };
  SMTemplate* t = make_tmpl("all-hooks", states, 3, trans, 2, IDLE);

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  /* Add hooks for all phases */
  sm_add_hook(sm, SM_HOOK_PRE_GUARD,   hook_pre_guard,   NULL);
  sm_add_hook(sm, SM_HOOK_POST_GUARD,  hook_post_guard,  NULL);
  sm_add_hook(sm, SM_HOOK_PRE_ACTION,  hook_pre_action,  NULL);
  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_post_action, NULL);
  sm_add_hook(sm, SM_HOOK_PRE_EMIT,    hook_pre_emit,    NULL);
  sm_add_hook(sm, SM_HOOK_POST_EMIT,   hook_post_emit,   NULL);

  hub_init();
  hub_subscribe(35, event_catcher, NULL);

  reset_hook_tracking();
  sm_transition(sm, WORK);

  /* All phases called */
  for (int i = 0; i < SM_HOOK_MAX; i++)
    assert(g_hook_calls[i] == 1);

  /* Correct order */
  assert(g_hook_order_count == 6);
  assert(g_hook_order[0] == SM_HOOK_PRE_GUARD);
  assert(g_hook_order[1] == SM_HOOK_POST_GUARD);
  assert(g_hook_order[2] == SM_HOOK_PRE_ACTION);
  assert(g_hook_order[3] == SM_HOOK_POST_ACTION);
  assert(g_hook_order[4] == SM_HOOK_PRE_EMIT);
  assert(g_hook_order[5] == SM_HOOK_POST_EMIT);

  hub_unsubscribe(35, event_catcher);

  /* Transition without guard still runs guard hooks */
  hub_subscribe(36, event_catcher, NULL);
  reset_hook_tracking();
  sm_transition(sm, DONE);
  for (int i = 0; i < SM_HOOK_MAX; i++)
    assert(g_hook_calls[i] == 1);

  hub_unsubscribe(36, event_catcher);
  hub_shutdown();
  sm_destroy(sm);
  sm_template_destroy(t);
  sm_registry_shutdown();
}

void test_hooks_userdata(void) {
  LOG_CLEAN("== sm hooks userdata");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 0 } };
  SMTemplate* t = make_tmpl("ud", states, 2, trans, 1, S0);

  int counter = 0;
  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_with_userdata, &counter);
  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_with_userdata, &counter);

  sm_transition(sm, S1);
  assert(counter == 2);

  sm_destroy(sm);
  sm_template_destroy(t);
}

void test_hooks_removal(void) {
  LOG_CLEAN("== sm hooks removal");
  uint32_t states[] = { S0, S1 };
  SMTransition trans[] = { { S0, S1, NULL, NULL, 0 } };
  SMTemplate* t = make_tmpl("removal", states, 2, trans, 1, S0);

  int called = 0;
  void hook_a(StateMachine* sm, void* ud) { (void)sm; (void)ud; called++; }

  int owner = 0;
  StateMachine* sm = make_sm(&owner, t);

  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_a, NULL);
  sm_add_hook(sm, SM_HOOK_POST_ACTION, hook_remove_test, NULL);

  called = 0;
  g_hook_remove_count = 0;
  sm_transition(sm, S1);
  assert(called == 1);
  assert(g_hook_remove_count == 1);

  /* Remove one hook */
  sm_remove_hook(sm, SM_HOOK_POST_ACTION, hook_remove_test);
  called = 0;
  g_hook_remove_count = 0;
  sm_raw_write(sm, S0);
  sm_transition(sm, S1);
  assert(called == 1);
  assert(g_hook_remove_count == 0); /* removed */

  sm_destroy(sm);
  sm_template_destroy(t);
}

int main(void) {
  test_tmpl_create_destroy();
  test_sm_create_destroy();
  test_sm_create_requires_params();
  test_sm_set_emitter();
  test_raw_write();
  test_raw_write_emits_via_hub();
  test_raw_write_with_custom_emitter();
  test_transition();
  test_can_transition();
  test_transition_with_guards();
  test_transition_with_actions();
  test_complete_flow();
  test_invalid_transition_rejected();
  test_hooks_basic();
  test_hooks_all_phases();
  test_hooks_userdata();
  test_hooks_removal();

  LOG_CLEAN("All SM tests passed!");
  return 0;
}