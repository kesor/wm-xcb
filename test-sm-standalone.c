#define DEBUG 1

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Mock running for LOG_FATAL */
int running = 1;

#define LOG_DEBUG(pFormat, ...) \
  { printf("DEBUG: " pFormat "\n", ##__VA_ARGS__); }

#define LOG_CLEAN(pFormat, ...)             \
  { printf(pFormat "\n", ##__VA_ARGS__); }

#define LOG_ERROR(pFormat, ...) \
  { fprintf(stderr, "ERROR: " pFormat "\n", ##__VA_ARGS__); }

#define LOG_FATAL(pFormat, ...)                       \
  {                                                   \
    fprintf(stderr, "FATAL: " pFormat "\n", ##__VA_ARGS__); \
    running = 0;                                      \
  }

#define assert(EXPRESSION)                                          \
  if (!(EXPRESSION)) {                                              \
    printf("%s:%d: %s - FAIL\n", __FILE__, __LINE__, #EXPRESSION); \
    exit(1);                                                        \
  } else {                                                          \
    printf("%s:%d: %s - pass\n", __FILE__, __LINE__, #EXPRESSION); \
  }

#include "src/sm/sm.h"

static int hook_called = 0;

void test_hook_fn(StateMachine* sm, void* userdata) {
  (void)sm;
  (void)userdata;
  hook_called++;
}

void test_sm_template_create_destroy() {
  LOG_CLEAN("== Testing SMTemplate create/destroy");

  enum { STATE_A = 0, STATE_B = 1 };

  uint32_t states[] = { STATE_A, STATE_B };
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
    STATE_A
  );

  assert(tmpl != NULL);
  assert(strcmp(sm_template_get_name(tmpl), "test-sm") == 0);
  assert(tmpl->num_states == 2);
  assert(tmpl->num_transitions == 2);
  assert(tmpl->initial_state == STATE_A);

  sm_template_destroy(tmpl);
}

void test_sm_instance_create_destroy() {
  LOG_CLEAN("== Testing StateMachine create/destroy");

  enum { STATE_A = 0, STATE_B = 1 };

  uint32_t states[] = { STATE_A, STATE_B };
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
    STATE_A
  );

  int owner = 42;
  StateMachine* sm = sm_create(&owner, tmpl);

  assert(sm != NULL);
  assert(strcmp(sm_get_name(sm), "test-instance") == 0);
  assert(sm_get_state(sm) == STATE_A);
  assert(sm->owner == &owner);
  assert(sm_get_template(sm) == tmpl);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void test_sm_raw_write() {
  LOG_CLEAN("== Testing sm_raw_write");

  enum { STATE_A = 0, STATE_B = 1 };

  uint32_t states[] = { STATE_A, STATE_B };
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
    STATE_A
  );

  int owner = 0;
  StateMachine* sm = sm_create(&owner, tmpl);

  assert(sm_get_state(sm) == STATE_A);

  sm_raw_write(sm, STATE_B);
  assert(sm_get_state(sm) == STATE_B);

  sm_raw_write(sm, STATE_A);
  assert(sm_get_state(sm) == STATE_A);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void test_sm_transition() {
  LOG_CLEAN("== Testing sm_transition");

  enum { STATE_OFF = 0, STATE_ON = 1 };

  uint32_t states[] = { STATE_OFF, STATE_ON };
  SMTransition transitions[] = {
    { STATE_OFF, STATE_ON, NULL, NULL, 100 },
    { STATE_ON, STATE_OFF, NULL, NULL, 101 },
  };

  SMTemplate* tmpl = sm_template_create(
    "test-transition",
    states,
    2,
    transitions,
    2,
    STATE_OFF
  );

  int owner = 0;
  StateMachine* sm = sm_create(&owner, tmpl);

  assert(sm_transition(sm, STATE_ON) == true);
  assert(sm_get_state(sm) == STATE_ON);

  assert(sm_transition(sm, STATE_OFF) == true);
  assert(sm_get_state(sm) == STATE_OFF);

  assert(sm_transition(sm, STATE_OFF) == false);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void test_sm_hooks() {
  LOG_CLEAN("== Testing sm hooks");

  enum { STATE_A = 0, STATE_B = 1 };

  uint32_t states[] = { STATE_A, STATE_B };
  SMTransition transitions[] = {
    { STATE_A, STATE_B, NULL, NULL, 0 },
  };

  SMTemplate* tmpl = sm_template_create(
    "test-hooks",
    states,
    2,
    transitions,
    1,
    STATE_A
  );

  int owner = 0;
  StateMachine* sm = sm_create(&owner, tmpl);

  hook_called = 0;
  sm_add_hook(sm, SM_HOOK_POST_ACTION, test_hook_fn, NULL);
  sm_transition(sm, STATE_B);
  assert(hook_called == 1);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

int main() {
  test_sm_template_create_destroy();
  test_sm_instance_create_destroy();
  test_sm_raw_write();
  test_sm_transition();
  test_sm_hooks();

  LOG_CLEAN("All SM tests passed!");
  return 0;
}
