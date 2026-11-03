#include <stdio.h>

#include "sm-debug.h"
#include "sm-instance.h"
#include "sm-template.h"
#include "sm-trace.h"
#include "wm-hub.h"

/*
 * Live machines, oldest created first. A machine is on this list for exactly as
 * long as it exists: sm_create() adds it, sm_destroy() removes it. Keeping the
 * list here rather than in the registry means the registry stays about guards
 * and actions, and nothing that runs per event walks it.
 */
static StateMachine* live_head  = NULL;
static StateMachine* live_tail  = NULL;
static uint32_t      live_count = 0;

void
sm_debug_machine_created(StateMachine* sm)
{
  if (sm == NULL)
    return;

  sm->next_live = NULL;
  if (live_tail == NULL)
    live_head = sm;
  else
    live_tail->next_live = sm;
  live_tail = sm;
  live_count++;
}

void
sm_debug_machine_destroyed(StateMachine* sm)
{
  if (sm == NULL)
    return;

  /* A pointer-to-pointer walk removes the machine without a previous pointer,
     which is what a singly linked list without a tail-predecessor needs. */
  StateMachine** link = &live_head;
  while (*link != NULL && *link != sm)
    link = &(*link)->next_live;

  if (*link == NULL)
    return;

  *link = sm->next_live;
  if (live_tail == sm) {
    live_tail = live_head;
    while (live_tail != NULL && live_tail->next_live != NULL)
      live_tail = live_tail->next_live;
  }
  if (live_count > 0)
    live_count--;

  sm->next_live = NULL;
}

uint32_t
sm_debug_live_count(void)
{
  return live_count;
}

void
sm_debug_note_enter(StateMachine* sm, uint32_t state)
{
  if (sm == NULL || sm->template == NULL)
    return;

  for (uint32_t i = 0; i < sm->template->num_states; i++) {
    if (sm->template->states[i].value != state)
      continue;

    if (i < SM_STATE_ENTER_MAX)
      sm->enter_counts[i]++;
    else
      sm->enter_counts_lost++;
    return;
  }
}

/*
 * Name the target that owns a machine. Client and Monitor both start with their
 * HubTarget, so a registered target's address is the owner pointer a component
 * passed to sm_create(). A test whose owner is a plain int finds nothing and
 * reports the address instead.
 */
static void
describe_owner(void* owner, const char** type_name, TargetID* id)
{
  *type_name = NULL;
  *id        = 0;

  uint32_t        type_count = 0;
  HubTargetType** types      = hub_get_all_target_types(&type_count);
  if (types == NULL)
    return;

  for (uint32_t i = 0; i < type_count; i++) {
    if (!types[i]->reserved)
      continue;

    HubTarget** targets = hub_get_targets_by_type(types[i]->id);
    if (targets == NULL)
      continue;

    for (uint32_t j = 0; targets[j] != NULL; j++) {
      if ((void*) targets[j] != owner)
        continue;

      *type_name = types[i]->name;
      *id        = targets[j]->id;
      return;
    }
  }
}

/*
 * A state as the dump prints it. The template's state list is the only place a
 * value's meaning is written down.
 */
static const char*
state_label(StateMachine* sm, uint32_t state, char* buffer, size_t size)
{
  const char* name = sm_template_state_name(sm->template, state);
  if (name != NULL)
    return name;

  snprintf(buffer, size, "%u", state);
  return buffer;
}

static const char*
via_label(SMTraceVia via)
{
  switch (via) {
  case SM_VIA_RAW_WRITE:
    return "raw";
  case SM_VIA_TRANSITION:
    return "transition";
  }
  return "?";
}

static const char*
guard_label(SMTraceGuard guard)
{
  switch (guard) {
  case SM_GUARD_NONE:
    return "none";
  case SM_GUARD_PASSED:
    return "passed";
  case SM_GUARD_REJECTED:
    return "rejected";
  }
  return "?";
}

static void
dump_counts(FILE* out, StateMachine* sm)
{
  char     label[32];
  uint32_t states = sm->template != NULL ? sm->template->num_states : 0;

  fprintf(out, "    entered:");
  for (uint32_t i = 0; i < states && i < SM_STATE_ENTER_MAX; i++)
    fprintf(out, " %s=%u", state_label(sm, sm->template->states[i].value, label, sizeof(label)),
            sm->enter_counts[i]);

  if (states > SM_STATE_ENTER_MAX || sm->enter_counts_lost > 0)
    fprintf(out, " (uncounted=%u)", sm->enter_counts_lost);
  fprintf(out, "\n");
}

static void
dump_trace(FILE* out, StateMachine* sm)
{
  SMTraceEntry entries[SM_TRACE_DEPTH];
  uint32_t     count = sm_trace_snapshot(sm, entries, SM_TRACE_DEPTH);
  char         from[32];
  char         to[32];

  fprintf(out, "    trace (oldest first, %u of %d kept):\n", count, SM_TRACE_DEPTH);

  for (uint32_t i = 0; i < count; i++) {
    SMTraceEntry* e = &entries[i];

    fprintf(out, "      %-10s %s -> %s  guard %s",
            via_label(e->via),
            state_label(sm, e->from_state, from, sizeof(from)),
            state_label(sm, e->to_state, to, sizeof(to)),
            guard_label(e->guard));

    if (!e->applied)
      fprintf(out, ", refused");
    else if (e->emit_event != 0)
      fprintf(out, ", event %u", e->emit_event);
    else
      fprintf(out, ", no event");

    fprintf(out, "\n");
  }
}

static void
dump_machine(FILE* out, StateMachine* sm)
{
  const char* type_name = NULL;
  TargetID    id        = 0;
  char        state[32];

  describe_owner(sm->owner, &type_name, &id);
  if (type_name != NULL)
    fprintf(out, "%s %llu\n", type_name, (unsigned long long) id);
  else
    fprintf(out, "owner %p\n", sm->owner);

  fprintf(out, "  %s: state %s\n", sm->name,
          state_label(sm, sm->current_state, state, sizeof(state)));

  dump_counts(out, sm);
  dump_trace(out, sm);
}

void
sm_debug_dump(FILE* out)
{
  if (out == NULL)
    return;

  fprintf(out, "state machines: %u live\n", live_count);

  for (StateMachine* sm = live_head; sm != NULL; sm = sm->next_live)
    dump_machine(out, sm);

  fflush(out);
}
