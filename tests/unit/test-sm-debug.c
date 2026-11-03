/*
 * State machine dump tests
 *
 * The dump is what SIGUSR1 prints in a running WM, so these assert the text a
 * reader gets: which machine, whose it is, the state it is in, how often it has
 * been in each state, and the last changes.
 */

#include <stdio.h>
#include <string.h>

#include "src/sm/sm-debug.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/sm/sm-trace.h"
#include "src/sm/sm.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"

enum { DUMP_IDLE = 0,
       DUMP_BUSY = 1,
};

enum { DUMP_EVT_START = 44,
       DUMP_EVT_STOP  = 45,
};

static void
dump_emitter(StateMachine* sm, uint32_t from, uint32_t to, void* userdata)
{
  (void) sm;
  (void) from;
  (void) to;
  (void) userdata;
}

static bool
dump_guard_allow(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

static SMTemplate*
dump_template(const char* name)
{
  static const SMStateName states[] = {
    { DUMP_IDLE, "IDLE" },
    { DUMP_BUSY, "BUSY" },
  };
  static SMTransition rows[] = {
    { DUMP_IDLE, DUMP_BUSY, "dump-allow", NULL, DUMP_EVT_START },
    { DUMP_BUSY, DUMP_IDLE, NULL,         NULL, DUMP_EVT_STOP  },
  };

  return sm_template_create(name, states, 2, rows, 2, DUMP_IDLE);
}

/* Capture one dump into `out`. Returns false when no temporary file can be
   opened, which would otherwise read as an empty dump. */
static bool
capture_dump(char* out, size_t size)
{
  FILE* stream = tmpfile();
  if (stream == NULL)
    return false;

  sm_debug_dump(stream);
  rewind(stream);

  size_t read = fread(out, 1, size - 1, stream);
  out[read]   = '\0';

  fclose(stream);
  return true;
}

/* Every group in this binary shares one process, so the live count before a
   test is not zero. Assert on what the dump says about the count, not on a
   number the rest of the suite decides. */
static void
assert_header_matches_live_count(const char* text)
{
  char expected[64];
  snprintf(expected, sizeof(expected), "state machines: %u live",
           sm_debug_live_count());
  assert(strstr(text, expected) != NULL);
}

static void
test_dump_reports_the_live_count(void)
{
  LOG_CLEAN("== dump reports the live count");

  char text[1024];
  assert_or_abort(capture_dump(text, sizeof(text)));

  assert_header_matches_live_count(text);
}

static void
test_dump_reports_owner_state_counts_and_trace(void)
{
  LOG_CLEAN("== dump reports owner, state, counts and trace");

  hub_init();
  sm_registry_init();
  sm_register_guard("dump-allow", dump_guard_allow);

  HubTarget target = {
    .id         = 4242,
    .type_id    = hub_get_target_type_id_by_name("client"),
    .registered = false,
  };
  hub_register_target(&target);

  SMTemplate* tmpl = dump_template("dump-sm");
  assert_or_abort(tmpl != NULL);

  uint32_t expected_live = sm_debug_live_count() + 1;

  StateMachine* sm = sm_create(&target, tmpl, dump_emitter, NULL);
  assert_or_abort(sm != NULL);

  assert(sm_transition(sm, DUMP_BUSY) == true);
  /* No row from BUSY to BUSY: the dump keeps the refused request. */
  assert(sm_transition(sm, DUMP_BUSY) == false);

  char text[4096];
  assert_or_abort(capture_dump(text, sizeof(text)));

  LOG_CLEAN("%s", text);

  assert(sm_debug_live_count() == expected_live);
  assert_header_matches_live_count(text);
  assert(strstr(text, "client 4242") != NULL);
  assert(strstr(text, "dump-sm: state BUSY") != NULL);
  /* The state it starts in counts once, then the change into BUSY. */
  assert(strstr(text, "entered: IDLE=1 BUSY=1") != NULL);
  assert(strstr(text, "transition IDLE -> BUSY") != NULL);
  assert(strstr(text, "guard passed") != NULL);
  assert(strstr(text, "event 44") != NULL);
  assert(strstr(text, "refused") != NULL);

  sm_destroy(sm);
  sm_template_destroy(tmpl);

  assert(sm_debug_live_count() == expected_live - 1);

  hub_unregister_target(target.id);
  sm_registry_shutdown();
  hub_shutdown();
}

static void
test_dump_lists_only_live_machines(void)
{
  LOG_CLEAN("== dump lists only the live machines");

  static const SMStateName states[] = {
    { DUMP_IDLE, "IDLE" },
    { DUMP_BUSY, "BUSY" },
  };
  SMTransition rows[] = {
    { DUMP_IDLE, DUMP_BUSY, NULL, NULL, DUMP_EVT_START },
  };
  SMTemplate* kept    = sm_template_create("kept-sm", states, 2, rows, 1, DUMP_IDLE);
  SMTemplate* dropped = sm_template_create("dropped-sm", states, 2, rows, 1, DUMP_IDLE);
  assert_or_abort(kept != NULL);
  assert_or_abort(dropped != NULL);

  int           owner  = 0;
  uint32_t      before = sm_debug_live_count();
  StateMachine* a      = sm_create(&owner, kept, dump_emitter, NULL);
  StateMachine* b      = sm_create(&owner, dropped, dump_emitter, NULL);
  assert_or_abort(a != NULL);
  assert_or_abort(b != NULL);

  assert(sm_debug_live_count() == before + 2);

  sm_destroy(a);
  assert(sm_debug_live_count() == before + 1);

  char text[4096];
  assert_or_abort(capture_dump(text, sizeof(text)));

  assert_header_matches_live_count(text);
  assert(strstr(text, "dropped-sm") != NULL);
  assert(strstr(text, "kept-sm") == NULL);

  sm_destroy(b);
  assert(sm_debug_live_count() == before);

  sm_template_destroy(kept);
  sm_template_destroy(dropped);
}

static void
test_dump_reports_an_unregistered_owner(void)
{
  LOG_CLEAN("== dump reports an owner that is not a hub target");

  static const SMStateName states[] = {
    { DUMP_IDLE, "IDLE" },
    { DUMP_BUSY, "BUSY" },
  };
  SMTransition rows[] = {
    { DUMP_IDLE, DUMP_BUSY, NULL, NULL, DUMP_EVT_START },
  };
  SMTemplate* tmpl = sm_template_create("ownerless-sm", states, 2, rows, 1, DUMP_IDLE);
  assert_or_abort(tmpl != NULL);

  /* A test owner, not a Client or Monitor: the dump says so instead of
     inventing a type and id. */
  int           owner = 7;
  StateMachine* sm    = sm_create(&owner, tmpl, dump_emitter, NULL);
  assert_or_abort(sm != NULL);

  char text[4096];
  assert_or_abort(capture_dump(text, sizeof(text)));

  assert(strstr(text, "owner 0x") != NULL);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
}

/* The creator dropping its reference must not free the template out from
   under a live machine. A dump reads the template; that is the path that
   faulted when shutdown freed the cache first. */
static void
test_dump_after_the_creator_drops_the_template(void)
{
  LOG_CLEAN("== a live machine keeps its template after the creator drops it");

  sm_register_guard("dump-allow", dump_guard_allow);

  SMTemplate* tmpl = dump_template("kept-alive");
  assert_or_abort(tmpl != NULL);

  int           owner = 0;
  StateMachine* sm    = sm_create(&owner, tmpl, dump_emitter, NULL);
  assert_or_abort(sm != NULL);

  sm_template_destroy(tmpl);

  assert(sm_transition(sm, DUMP_BUSY) == true);

  char text[2048];
  assert_or_abort(capture_dump(text, sizeof(text)));
  assert(strstr(text, "kept-alive: state BUSY") != NULL);

  sm_destroy(sm);
}

TEST_GROUP(SMDebug, {
  sm_registry_init();

  test_dump_reports_the_live_count();
  test_dump_after_the_creator_drops_the_template();
  test_dump_reports_owner_state_counts_and_trace();
  test_dump_lists_only_live_machines();
  test_dump_reports_an_unregistered_owner();

  sm_registry_shutdown();
});
