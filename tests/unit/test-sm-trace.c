/*
 * State machine trace tests
 *
 * The trace is what a state dump prints, so these assert the record itself:
 * which pair changed, whether it was reality or a request, what the guard
 * answered, and which event went out.
 */

#include <string.h>

#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/sm/sm-trace.h"
#include "src/sm/sm.h"
#include "test-registry.h"
#include "test-wm.h"

enum { TRACE_OFF  = 0,
       TRACE_ON   = 1,
       TRACE_DEAD = 2,
};

enum { TRACE_EVT_OFF = 40,
       TRACE_EVT_ON  = 41,
};

static void
trace_emitter(StateMachine* sm, uint32_t from, uint32_t to, void* userdata)
{
  (void) sm;
  (void) from;
  (void) to;
  int* calls = (int*) userdata;
  if (calls != NULL)
    (*calls)++;
}

static bool
trace_guard_allow(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

static bool
trace_guard_deny(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return false;
}

static bool
trace_action_fail(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return false;
}

static SMTemplate*
trace_template(const char* name, SMTransition* rows, uint32_t num_rows)
{
  static const SMStateName states[] = {
    { TRACE_OFF,  "OFF"  },
    { TRACE_ON,   "ON"   },
    { TRACE_DEAD, "DEAD" },
  };
  return sm_template_create(name, states, 3, rows, num_rows, TRACE_OFF);
}

static void
test_trace_raw_write(void)
{
  LOG_CLEAN("== trace records a raw write");

  SMTransition rows[] = {
    { TRACE_OFF, TRACE_ON,  NULL, NULL, TRACE_EVT_ON  },
    { TRACE_ON,  TRACE_OFF, NULL, NULL, TRACE_EVT_OFF },
  };
  SMTemplate*   tmpl    = trace_template("trace-raw", rows, 2);
  int           owner   = 0;
  int           emitted = 0;
  StateMachine* sm      = sm_create(&owner, tmpl, trace_emitter, &emitted);
  assert_or_abort(sm != NULL);

  assert(sm_trace_count(sm) == 0);

  sm_raw_write(sm, TRACE_ON);

  assert(sm_trace_count(sm) == 1);

  SMTraceEntry entry;
  assert(sm_trace_snapshot(sm, &entry, 1) == 1);
  assert(entry.from_state == TRACE_OFF);
  assert(entry.to_state == TRACE_ON);
  assert(entry.via == SM_VIA_RAW_WRITE);
  assert(entry.guard == SM_GUARD_NONE);
  assert(entry.applied == true);
  assert(entry.emit_event == TRACE_EVT_ON);
  assert(emitted == 1);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

void
test_trace_transition(void)
{
  LOG_CLEAN("== trace records a transition and its guard answer");

  SMTransition rows[] = {
    { TRACE_OFF, TRACE_ON, "trace-allow", NULL, TRACE_EVT_ON },
  };
  SMTemplate*   tmpl  = trace_template("trace-trans", rows, 1);
  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  assert(sm_transition(sm, TRACE_ON) == true);

  SMTraceEntry entry;
  assert(sm_trace_snapshot(sm, &entry, 1) == 1);
  assert(entry.via == SM_VIA_TRANSITION);
  assert(entry.guard == SM_GUARD_PASSED);
  assert(entry.applied == true);
  assert(entry.emit_event == TRACE_EVT_ON);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

static void
test_trace_refused_transition(void)
{
  LOG_CLEAN("== trace keeps a refused transition");

  /* A request with no row at all: the pair is not declared. */
  SMTransition rows[] = {
    { TRACE_ON, TRACE_OFF, NULL, NULL, TRACE_EVT_OFF },
  };
  SMTemplate*   tmpl  = trace_template("trace-refused", rows, 1);
  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  assert(sm_transition(sm, TRACE_ON) == false);
  assert(sm_get_state(sm) == TRACE_OFF);

  SMTraceEntry entry;
  assert(sm_trace_snapshot(sm, &entry, 1) == 1);
  assert(entry.from_state == TRACE_OFF);
  assert(entry.to_state == TRACE_ON);
  assert(entry.via == SM_VIA_TRANSITION);
  assert(entry.guard == SM_GUARD_NONE);
  assert(entry.applied == false);
  assert(entry.emit_event == 0);

  sm_destroy(sm);
  sm_template_destroy(tmpl);

  /* A declared row whose guard said no. */
  SMTransition guarded[] = {
    { TRACE_OFF, TRACE_ON, "trace-deny", NULL, TRACE_EVT_ON },
  };
  tmpl = trace_template("trace-denied", guarded, 1);
  sm   = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  assert(sm_transition(sm, TRACE_ON) == false);
  assert(sm_get_state(sm) == TRACE_OFF);

  assert(sm_trace_snapshot(sm, &entry, 1) == 1);
  assert(entry.guard == SM_GUARD_REJECTED);
  assert(entry.applied == false);
  assert(entry.emit_event == 0);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

static void
test_trace_failed_action_names_the_guard_that_ran(void)
{
  LOG_CLEAN("== a failed action records the guard that ran, not a guard that did not");

  /* No guard on the row: the refusal is the action, so the guard label is none. */
  SMTransition rows[] = {
    { TRACE_OFF, TRACE_ON, NULL, "trace-fail", TRACE_EVT_ON },
  };
  SMTemplate*   tmpl  = trace_template("trace-action", rows, 1);
  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  assert(sm_transition(sm, TRACE_ON) == false);
  assert(sm_get_state(sm) == TRACE_OFF);

  SMTraceEntry entry;
  assert(sm_trace_snapshot(sm, &entry, 1) == 1);
  assert(entry.via == SM_VIA_TRANSITION);
  assert(entry.guard == SM_GUARD_NONE);
  assert(entry.applied == false);
  assert(entry.emit_event == 0);

  sm_destroy(sm);
  sm_template_destroy(tmpl);

  /* A guard that passed, then an action that failed. */
  SMTransition guarded[] = {
    { TRACE_OFF, TRACE_ON, "trace-allow", "trace-fail", TRACE_EVT_ON },
  };
  tmpl = trace_template("trace-action-guarded", guarded, 1);
  sm   = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  assert(sm_transition(sm, TRACE_ON) == false);
  assert(sm_trace_snapshot(sm, &entry, 1) == 1);
  assert(entry.guard == SM_GUARD_PASSED);
  assert(entry.applied == false);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

static void
test_trace_ring_wraps(void)
{
  LOG_CLEAN("== trace keeps the newest entries when the ring is full");

  SMTransition rows[] = {
    { TRACE_OFF, TRACE_ON,  NULL, NULL, TRACE_EVT_ON  },
    { TRACE_ON,  TRACE_OFF, NULL, NULL, TRACE_EVT_OFF },
  };
  SMTemplate*   tmpl  = trace_template("trace-ring", rows, 2);
  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  /* Two more changes than the ring holds. */
  uint32_t changes = SM_TRACE_DEPTH + 2;
  for (uint32_t i = 0; i < changes; i++)
    sm_raw_write(sm, (i % 2 == 0) ? TRACE_ON : TRACE_OFF);

  assert(sm_trace_count(sm) == SM_TRACE_DEPTH);

  SMTraceEntry entries[SM_TRACE_DEPTH];
  assert(sm_trace_snapshot(sm, entries, SM_TRACE_DEPTH) == SM_TRACE_DEPTH);

  /* Oldest first: the first entry kept is the third change. */
  for (uint32_t i = 0; i < SM_TRACE_DEPTH; i++) {
    uint32_t change      = i + (changes - SM_TRACE_DEPTH);
    uint32_t expected_to = (change % 2 == 0) ? TRACE_ON : TRACE_OFF;
    assert(entries[i].to_state == expected_to);
  }

  /* The last entry is the most recent change. */
  assert(entries[SM_TRACE_DEPTH - 1].to_state == TRACE_OFF);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

static void
test_trace_snapshot_capacity(void)
{
  LOG_CLEAN("== trace snapshot copies the newest entries that fit");

  SMTransition rows[] = {
    { TRACE_OFF, TRACE_ON, NULL, NULL, TRACE_EVT_ON },
  };
  SMTemplate*   tmpl  = trace_template("trace-capacity", rows, 1);
  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, trace_emitter, NULL);
  assert_or_abort(sm != NULL);

  sm_raw_write(sm, TRACE_ON);
  sm_raw_write(sm, TRACE_DEAD);

  SMTraceEntry two[2];
  assert(sm_trace_snapshot(sm, two, 2) == 2);
  assert(two[0].to_state == TRACE_ON);
  assert(two[1].to_state == TRACE_DEAD);

  SMTraceEntry one[1];
  assert(sm_trace_snapshot(sm, one, 1) == 1);
  assert(one[0].to_state == TRACE_DEAD);

  /* A destroyed machine is gone, and a NULL machine records nothing. */
  sm_destroy(sm);
  assert(sm_trace_count(NULL) == 0);
  assert(sm_trace_snapshot(NULL, two, 2) == 0);

  sm_template_destroy(tmpl);
}

TEST_GROUP(SMTrace, {
  sm_registry_init();
  sm_register_guard("trace-allow", trace_guard_allow);
  sm_register_guard("trace-deny", trace_guard_deny);
  sm_register_action("trace-fail", trace_action_fail);

  test_trace_raw_write();
  test_trace_transition();
  test_trace_refused_transition();
  test_trace_failed_action_names_the_guard_that_ran();
  test_trace_ring_wraps();
  test_trace_snapshot_capacity();

  sm_registry_shutdown();
});
