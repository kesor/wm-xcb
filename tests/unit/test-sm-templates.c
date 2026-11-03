/*
 * State machine template contract tests
 *
 * docs/PHILOSOPHY.md section 5 says a reader can know every reachable state and
 * every way out of a template without reading a guard body. That only holds if
 * the table is complete, so these tests hold it to that:
 *
 *  - a template with an unreachable state, or a row that emits nothing, fails
 *    validation and cannot be compiled from a table definition
 *  - every template the tree ships passes validation
 *  - every template ships its text diagram in the header, and the diagram names
 *    the states and rows the table has (PHILOSOPHY 5.10)
 *  - every row of every machine, driven for real, emits the event it names
 *
 * The walk tests use the component's own instance, so a guard that refuses a
 * declared row, or an emitter that sends a different event than the row names,
 * fails here rather than in a running WM.
 */

#include "test-registry.h" /* Must be first - defines TEST_GROUP macro */

#include <ctype.h>
#include <string.h>

#include "src/actions/action-registry.h"
#include "src/components/client-list.h"
#include "src/components/connection-sm.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/sm/sm.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "test-wm.h"
#include "wm-hub.h"

/* ==================== validation ==================== */

enum { WALK_A = 0,
       WALK_B = 1,
       WALK_C = 2,
};

static void
test_validate_accepts_a_complete_table(void)
{
  LOG_CLEAN("== a complete table validates");

  static const SMStateName states[] = {
    { WALK_A, "A" },
    { WALK_B, "B" },
  };
  static const SMTransition rows[] = {
    { WALK_A, WALK_B, NULL, NULL, 50 },
    { WALK_B, WALK_A, NULL, NULL, 51 },
  };
  static const SMTemplateDef def =
      SM_TEMPLATE_DEF("complete", WALK_A, states, rows);

  SMTemplate* tmpl = sm_template_from_def(&def);
  assert_or_abort(tmpl != NULL);

  char reason[160];
  assert(sm_template_validate(tmpl, reason, sizeof(reason)) == true);
  assert(reason[0] == '\0');

  sm_template_destroy(tmpl);
}

static void
test_validate_rejects_an_unreachable_state(void)
{
  LOG_CLEAN("== a state nothing enters is rejected");

  static const SMStateName states[] = {
    { WALK_A, "A" },
    { WALK_B, "B" },
    { WALK_C, "C" },
  };
  /* C is declared and never entered: either it is dead, or a row is missing. */
  static const SMTransition rows[] = {
    { WALK_A, WALK_B, NULL, NULL, 50 },
    { WALK_B, WALK_A, NULL, NULL, 51 },
  };
  static const SMTemplateDef def =
      SM_TEMPLATE_DEF("unreachable", WALK_A, states, rows);

  SMTemplate* tmpl = sm_template_create(def.name, def.states, def.num_states,
                                        def.rows, def.num_rows, def.initial_state);
  assert_or_abort(tmpl != NULL);

  char reason[160];
  assert(sm_template_validate(tmpl, reason, sizeof(reason)) == false);
  assert(strstr(reason, "unreachable") != NULL);

  sm_template_destroy(tmpl);

  /* The table compiler refuses it, so a component cannot come up on it. */
  assert(sm_template_from_def(&def) == NULL);
}

static void
test_validate_rejects_a_silent_transition(void)
{
  LOG_CLEAN("== a transition with no event is rejected");

  static const SMStateName states[] = {
    { WALK_A, "A" },
    { WALK_B, "B" },
  };
  static const SMTransition rows[] = {
    { WALK_A, WALK_B, NULL, NULL, 0  },
    { WALK_B, WALK_A, NULL, NULL, 51 },
  };
  static const SMTemplateDef def =
      SM_TEMPLATE_DEF("silent", WALK_A, states, rows);

  SMTemplate* tmpl = sm_template_create(def.name, def.states, def.num_states,
                                        def.rows, def.num_rows, def.initial_state);
  assert_or_abort(tmpl != NULL);

  char reason[160];
  assert(sm_template_validate(tmpl, reason, sizeof(reason)) == false);
  assert(strstr(reason, "emits no event") != NULL);

  sm_template_destroy(tmpl);
  assert(sm_template_from_def(&def) == NULL);
}

static void
test_validate_rejects_an_undeclared_state(void)
{
  LOG_CLEAN("== a row that names a state outside the table is rejected");

  static const SMStateName states[] = {
    { WALK_A, "A" },
    { WALK_B, "B" },
  };
  static const SMTransition rows[] = {
    { WALK_A, WALK_B, NULL, NULL, 50 },
    { WALK_B, WALK_C, NULL, NULL, 51 },
  };
  static const SMTemplateDef def =
      SM_TEMPLATE_DEF("undeclared", WALK_A, states, rows);

  SMTemplate* tmpl = sm_template_create(def.name, def.states, def.num_states,
                                        def.rows, def.num_rows, def.initial_state);
  assert_or_abort(tmpl != NULL);

  char reason[160];
  assert(sm_template_validate(tmpl, reason, sizeof(reason)) == false);
  assert(strstr(reason, "undeclared state") != NULL);

  sm_template_destroy(tmpl);
}

static void
test_validate_rejects_an_undeclared_initial_state(void)
{
  LOG_CLEAN("== an initial state outside the table is rejected");

  static const SMStateName states[] = {
    { WALK_A, "A" },
  };
  static const SMTemplateDef def =
      SM_TEMPLATE_DEF_STATES("bad-initial", WALK_C, states);

  SMTemplate* tmpl = sm_template_create(def.name, def.states, def.num_states,
                                        def.rows, def.num_rows, def.initial_state);
  assert_or_abort(tmpl != NULL);

  char reason[160];
  assert(sm_template_validate(tmpl, reason, sizeof(reason)) == false);
  assert(strstr(reason, "initial state") != NULL);

  sm_template_destroy(tmpl);
}

/* ==================== the templates this tree ships ==================== */

static void check_diagram(const char* machine, const char* header_path, SMTemplate* tmpl);

static void
test_template_is_valid(const char* what, SMTemplate* tmpl)
{
  LOG_CLEAN("== template '%s' passes validation", what);
  assert_or_abort(tmpl != NULL);

  char reason[160];
  bool valid = sm_template_validate(tmpl, reason, sizeof(reason));
  if (!valid)
    LOG_CLEAN("%s: %s", what, reason);
  assert(valid == true);

  /* Every state has a name, or the dump prints a number nobody can read. */
  for (uint32_t i = 0; i < tmpl->num_states; i++)
    assert(tmpl->states[i].name != NULL);
}

static void
test_shipped_templates_are_valid(void)
{
  LOG_CLEAN("== every shipped template is valid");

  hub_init();
  sm_registry_init();
  monitor_list_init();

  SMTemplate* focus_tmpl = focus_sm_template_create();
  test_template_is_valid("focus", focus_tmpl);
  check_diagram("focus", "src/components/focus.h", focus_tmpl);
  sm_template_destroy(focus_tmpl);

  SMTemplate* fullscreen_tmpl = fullscreen_sm_template_create();
  test_template_is_valid("fullscreen", fullscreen_tmpl);
  check_diagram("fullscreen", "src/components/fullscreen.h", fullscreen_tmpl);
  sm_template_destroy(fullscreen_tmpl);

  SMTemplate* connection_tmpl = connection_sm_template_create();
  test_template_is_valid("connection", connection_tmpl);
  check_diagram("connection", "src/components/connection-sm.h", connection_tmpl);
  sm_template_destroy(connection_tmpl);

  Monitor* m = monitor_create(900);
  assert_or_abort(m != NULL);

  StateMachine* view_sm = tag_manager_get_view_sm(m);
  assert_or_abort(view_sm != NULL);
  test_template_is_valid("tag-view", sm_get_template(view_sm));
  check_diagram("tag-view", "src/components/tag-manager.h", sm_get_template(view_sm));

  StateMachine* layout_sm = tiling_get_sm(m);
  assert_or_abort(layout_sm != NULL);
  test_template_is_valid("layout", sm_get_template(layout_sm));
  check_diagram("layout", "src/components/tiling.h", sm_get_template(layout_sm));

  monitor_destroy(m);
  monitor_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

/* ==================== the diagram next to the template ==================== */

/*
 * The convention: a template's doc comment opens with
 *
 *   State machine: <template name>
 *   FROM -> TO      event <name>   guard <name>
 *   ...
 *
 * and the diagram is those lines, up to the first empty comment line. Prose
 * below that line is for the reader and is not checked, so it may name a state
 * without the check mistaking it for the diagram. The lines have to be there
 * because they are what a person reads when they open the header; the check is
 * what keeps them equal to the table.
 */
static bool
diagram_block(const char* header_path, const char* machine, char* out, size_t size)
{
  FILE* file = fopen(header_path, "r");
  if (file == NULL) {
    LOG_CLEAN("cannot read %s", header_path);
    return false;
  }

  static char source[65536];
  size_t      read = fread(source, 1, sizeof(source) - 1, file);
  source[read]     = '\0';
  fclose(file);

  char marker[64];
  snprintf(marker, sizeof(marker), "State machine: %s", machine);

  const char* start = strstr(source, marker);
  if (start == NULL)
    return false;

  /* The diagram ends at the first comment line that carries nothing after the
     asterisk. */
  const char* line = strchr(start, '\n');
  const char* end  = NULL;
  while (line != NULL) {
    const char* next = strchr(line + 1, '\n');
    const char* from = line + 1;
    size_t      span = next != NULL ? (size_t) (next - from) : strlen(from);

    char   trimmed[256];
    size_t copy = span < sizeof(trimmed) - 1 ? span : sizeof(trimmed) - 1;
    memcpy(trimmed, from, copy);
    trimmed[copy] = '\0';

    char* text = trimmed;
    while (*text == ' ' || *text == '\t')
      text++;
    if (*text == '*')
      text++;
    while (*text == ' ' || *text == '\t')
      text++;

    bool blank = *text == '\0' || strncmp(text, "*/", 2) == 0;
    if (blank) {
      end = from;
      break;
    }

    line = next;
  }

  if (end == NULL)
    end = start + strlen(start);

  size_t length = (size_t) (end - start);
  if (length >= size)
    length = size - 1;

  memcpy(out, start, length);
  out[length] = '\0';
  return true;
}

/* Name match that does not fire on a longer name containing it: a diagram that
   says TILES has not named the state TILE. */
static bool
has_word(const char* text, const char* word)
{
  size_t length = strlen(word);

  for (const char* found = strstr(text, word); found != NULL; found = strstr(found + 1, word)) {
    bool starts = found == text || !(isalnum((unsigned char) found[-1]) || found[-1] == '_');
    bool ends   = !(isalnum((unsigned char) found[length]) || found[length] == '_');
    if (starts && ends)
      return true;
  }

  return false;
}

static bool
line_draws_row(const char* line, size_t length, const char* from, const char* to)
{
  char buffer[512];

  if (length >= sizeof(buffer))
    length = sizeof(buffer) - 1;
  memcpy(buffer, line, length);
  buffer[length] = '\0';

  if (!has_word(buffer, from) || !has_word(buffer, to))
    return false;

  /* Direction matters: the row is the state it leaves, then the state it
     enters. A diagram that has them the other way is about another row. A row
     that stays in its state has one position, so the comparison includes it. */
  return strstr(buffer, from) <= strstr(buffer, to);
}

static bool
diagram_draws_row(const char* block, const char* from, const char* to)
{
  const char* line = block;

  while (line != NULL && *line != '\0') {
    const char* end  = strchr(line, '\n');
    size_t      size = end != NULL ? (size_t) (end - line) : strlen(line);

    if (line_draws_row(line, size, from, to))
      return true;

    line = end != NULL ? end + 1 : NULL;
  }

  return false;
}

static void
check_diagram(const char* machine, const char* header_path, SMTemplate* tmpl)
{
  LOG_CLEAN("== '%s' ships a diagram of itself", machine);

  char* block = calloc(1, 8192);
  assert_or_abort(block != NULL);

  bool found = diagram_block(header_path, machine, block, 8192);
  if (!found)
    LOG_CLEAN("%s: no comment block starting 'State machine: %s'", header_path, machine);
  assert(found == true);

  for (uint32_t i = 0; i < tmpl->num_states; i++) {
    const char* state = tmpl->states[i].name;
    assert_or_abort(state != NULL);
    if (!has_word(block, state))
      LOG_CLEAN("%s: the diagram does not name state %s", header_path, state);
    assert(has_word(block, state) == true);
  }

  for (uint32_t r = 0; r < tmpl->num_transitions; r++) {
    const char* from = sm_template_state_name(tmpl, tmpl->transitions[r].from_state);
    const char* to   = sm_template_state_name(tmpl, tmpl->transitions[r].to_state);
    assert_or_abort(from != NULL);
    assert_or_abort(to != NULL);

    if (!diagram_draws_row(block, from, to))
      LOG_CLEAN("%s: the diagram has no line '%s -> %s'", header_path, from, to);
    assert(diagram_draws_row(block, from, to) == true);
  }

  free(block);
}

/* ==================== walking every declared row ==================== */

static struct {
  uint32_t  count;
  EventType last;
} seen;

static void
record_event(Event e)
{
  seen.count++;
  seen.last = e.type;
}

/* Subscribe to whatever events the template names, so the walk sees what
   actually reaches the hub rather than what the row claims. */
static uint32_t
subscribe_to_template_events(const SMTemplate* tmpl, EventType* subscribed, uint32_t max)
{
  uint32_t n = 0;

  for (uint32_t r = 0; r < tmpl->num_transitions; r++) {
    EventType event = tmpl->transitions[r].emit_event;
    bool      known = false;

    for (uint32_t i = 0; i < n; i++) {
      if (subscribed[i] == event)
        known = true;
    }
    if (known)
      continue;

    assert_or_abort(n < max);
    hub_subscribe(event, record_event, NULL);
    subscribed[n++] = event;
  }

  return n;
}

static void
walk_rows(const char* machine, StateMachine* sm)
{
  LOG_CLEAN("== every row of '%s' emits the event it names", machine);

  SMTemplate* tmpl = sm_get_template(sm);
  assert_or_abort(tmpl != NULL);

  EventType subscribed[16];
  uint32_t  count = subscribe_to_template_events(tmpl, subscribed, 16);

  for (uint32_t r = 0; r < tmpl->num_transitions; r++) {
    const SMTransition* row = &tmpl->transitions[r];

    /* Park the machine in the row's from_state. The raw write may emit an
       event of its own; the counter is cleared after it. */
    sm_raw_write(sm, row->from_state);
    seen.count = 0;
    seen.last  = 0;

    bool moved = sm_transition(sm, row->to_state);
    if (!moved)
      LOG_CLEAN("%s row %u: %u -> %u was refused", machine, r, row->from_state, row->to_state);
    assert(moved == true);
    assert(sm_get_state(sm) == row->to_state);

    if (seen.count != 1 || seen.last != row->emit_event)
      LOG_CLEAN("%s row %u: expected event %u, saw %u event(s), last %u",
                machine, r, row->emit_event, seen.count, seen.last);
    assert(seen.count == 1);
    assert(seen.last == row->emit_event);
  }

  for (uint32_t i = 0; i < count; i++)
    hub_unsubscribe(subscribed[i], record_event);
}

static void
test_focus_rows_emit_their_events(void)
{
  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  focus_component_init();

  Client* c = client_create(100);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);
  client_set_focusable(c, true);
  client_set_mapped(c, true);

  StateMachine* sm = focus_get_sm(c);
  assert_or_abort(sm != NULL);

  walk_rows("focus", sm);

  client_list_shutdown();
  focus_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

static void
test_fullscreen_rows_emit_their_events(void)
{
  hub_init();
  sm_registry_init();
  xcb_handler_init();
  client_list_init();
  action_registry_init();
  fullscreen_component_init();

  Client* c = client_create(101);
  assert_or_abort(c != NULL);
  client_set_managed(c, true);

  StateMachine* sm = fullscreen_get_sm(c);
  assert_or_abort(sm != NULL);

  walk_rows("fullscreen", sm);

  client_list_shutdown();
  fullscreen_component_shutdown();
  action_registry_shutdown();
  xcb_handler_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

static void
test_tag_view_rows_emit_their_events(void)
{
  hub_init();
  sm_registry_init();
  monitor_list_init();
  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(901);
  assert_or_abort(m != NULL);

  StateMachine* sm = tag_manager_get_view_sm(m);
  assert_or_abort(sm != NULL);

  walk_rows("tag-view", sm);

  monitor_destroy(m);
  monitor_list_shutdown();
  tag_manager_component_shutdown();
  action_registry_init();
  action_registry_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

static void
test_layout_has_no_rows_to_walk(void)
{
  hub_init();
  sm_registry_init();
  monitor_list_init();

  Monitor* m = monitor_create(902);
  assert_or_abort(m != NULL);

  StateMachine* sm = tiling_get_sm(m);
  assert_or_abort(sm != NULL);

  SMTemplate* tmpl = sm_get_template(sm);
  assert_or_abort(tmpl != NULL);
  assert(tmpl->num_transitions == 0);
  assert(tmpl->num_states == 1);
  assert(sm_get_state(sm) == tmpl->initial_state);

  monitor_destroy(m);
  monitor_list_shutdown();
  sm_registry_shutdown();
  hub_shutdown();
}

static void
test_connection_rows_emit_their_events(void)
{
  hub_init();
  sm_registry_init();

  SMTemplate* tmpl = connection_sm_template_create();
  assert_or_abort(tmpl != NULL);

  /* No component owns a connection machine yet, so the framework's own emitter
     is the one under test: it emits through the hub with the row's event. */
  HubTarget target = {
    .id         = 703,
    .type_id    = hub_get_target_type_id_by_name("monitor"),
    .registered = false,
  };
  StateMachine* sm = sm_create(&target, tmpl, NULL, NULL);
  assert_or_abort(sm != NULL);

  walk_rows("connection", sm);

  sm_destroy(sm);
  sm_template_destroy(tmpl);
  sm_registry_shutdown();
  hub_shutdown();
}

TEST_GROUP(SMTemplates, {
  test_validate_accepts_a_complete_table();
  test_validate_rejects_an_unreachable_state();
  test_validate_rejects_a_silent_transition();
  test_validate_rejects_an_undeclared_state();
  test_validate_rejects_an_undeclared_initial_state();

  test_shipped_templates_are_valid();

  test_focus_rows_emit_their_events();
  test_fullscreen_rows_emit_their_events();
  test_tag_view_rows_emit_their_events();
  test_layout_has_no_rows_to_walk();
  test_connection_rows_emit_their_events();
});
