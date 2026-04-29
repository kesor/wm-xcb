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

#define assert(EXPRESSION)                                         \
  if (!(EXPRESSION)) {                                             \
    printf("%s:%d: %s - FAIL\n", __FILE__, __LINE__, #EXPRESSION); \
    exit(1);                                                       \
  } else {                                                         \
    printf("%s:%d: %s - pass\n", __FILE__, __LINE__, #EXPRESSION); \
  }

#include "src/sm/sm.h"
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
static uint32_t
test_emitter(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) sm;
  (void) from_state;
  (void) to_state;
  (void) userdata;
  LOG_DEBUG("Custom emitter called");
  raw_write_events_emitted++;
  return 100; /* custom event type */
}

void
test_hook_fn(StateMachine* sm, void* userdata)
{
  (void) sm;
  (void) userdata;
  hook_called++;
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

  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, NULL, NULL);

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
  StateMachine* sm2 = sm_create(&owner, tmpl, test_emitter, NULL);

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

int
main()
{
  test_sm_template_create_destroy();
  test_sm_instance_create_destroy();
  test_sm_raw_write();
  test_sm_raw_write_event_emission();
  test_sm_transition();
  test_sm_hooks();

  LOG_CLEAN("All SM tests passed!");
  return 0;
}
