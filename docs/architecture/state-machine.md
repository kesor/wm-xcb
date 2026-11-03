# State Machine Framework

*Part of architecture documentation — Authoritative*
*Last updated: 2026-11-03*

> **💡 Paradigm Shift:** State machines are the only authority on state mutations. No direct manipulation. No hidden state. Every state change goes through a SM with guards and events.

---

## Purpose

State machines are the **only authority on state mutations**. They track mutually exclusive state with:
- Well-defined transitions
- Guards to validate transitions
- Actions on transition
- Events emitted on state change

**Key design principle:** State machines live on targets, not in components. Components provide SM templates; targets adopt and own the SMs.

---

## Core Concepts

### State Machine Instance
A running instance that holds:
- Current state
- Pointer to owner target
- Pointer to template (defines states, transitions, guards)

### SM Template
Defines the structure:
- List of states
- List of transitions
- Guards for each transition
- Actions for each transition
- Events emitted on each transition

### Mutually Exclusive State
Within one SM, the system is in exactly one state at a time:
```
ClientFullscreenSM: FULLSCREEN ←→ WINDOWED
ClientFloatingSM: FLOATING ←→ TILED
```

These are independent SMs. A client can be simultaneously:
- FULLSCREEN (FullscreenSM)
- TILED (FloatingSM)
- FOCUSED (FocusSM)

---

## SM Instance Structure

The fields are `struct StateMachine` in `src/sm/sm-instance.h`. This document
does not keep a second copy of that struct.

### Trace

Every machine keeps its last `SM_TRACE_DEPTH` (8) changes in a ring: the pair
that changed, whether it was a raw write or a transition, what the guard
answered, and which event went out. A refused transition is kept too, with
`applied` false, so a request that did nothing is a record instead of a
silence. `src/sm/sm-trace.h` defines the entry; `sm_trace_snapshot()` copies
the ring oldest first. The ring is a fixed array inside the machine, so
recording costs no allocation on a path that runs per event.

### Dump

`sm_debug_dump(FILE*)` prints every live machine: the target that owns it, its
current state, how often it has entered each state, and its trace. The WM calls
it when SIGUSR1 arrives, from the event loop rather than from the handler, and
writes to stdout; `tests/unit/test-sm-debug.c` checks the text and
`tests/x/test-15-state-dump.sh` checks it against a running WM.

A template counts as alive as long as one machine uses it: the creator and every
machine hold a reference, so a component that drops its cached template at
shutdown cannot leave a live machine pointing at freed memory.

---

## SM Template Structure

A template is two tables and the state the machine starts in. The counts come
from the tables (`SM_TABLE_COUNT`), so adding a row is one edit and not three.

```c
typedef struct SMStateName { uint32_t value; const char* name; } SMStateName;
typedef struct SMTransition {
    uint32_t from_state;
    uint32_t to_state;
    const char* guard_fn;   // name of guard function, or NULL
    const char* action_fn;  // name of action function, or NULL
    uint32_t emit_event;    // the event subscribers hear about this change
} SMTransition;

typedef struct SMTemplateDef {
    const char*         name;
    uint32_t            initial_state;
    const SMStateName*  states;
    uint32_t            num_states;
    const SMTransition* rows;
    uint32_t            num_rows;
} SMTemplateDef;
```

A template reads as a table, and the state names are the words its diagram uses:

```c
static const SMStateName states[] = {
  { FOCUS_STATE_UNFOCUSED, "UNFOCUSED" },
  { FOCUS_STATE_FOCUSED,   "FOCUSED"   },
};
static const SMTransition rows[] = {
  { FOCUS_STATE_UNFOCUSED, FOCUS_STATE_FOCUSED, "focus_guard_can_focus", NULL, EVT_CLIENT_FOCUSED   },
  { FOCUS_STATE_FOCUSED, FOCUS_STATE_UNFOCUSED, "focus_guard_can_unfocus", NULL, EVT_CLIENT_UNFOCUSED },
};
static const SMTemplateDef def =
    SM_TEMPLATE_DEF("focus", FOCUS_STATE_UNFOCUSED, states, rows);
```

`sm_template_from_def()` compiles that and validates it: the initial state and
both ends of every row must be declared, every row must emit an event, and every
declared state must be reachable from the initial state. A table that fails is
logged and the component does not come up on it. `sm_template_validate()` is the
check, and `tests/unit/test-sm-templates.c` holds the shipped templates to it.

A machine with one state and no transitions uses `SM_TEMPLATE_DEF_STATES()`: a
zero-length row table is a GNU extension the `-pedantic` build rejects.

`sm_template_create()` remains the constructor the macros compile to. It does
not check the table, because the framework's own tests build machines the
contract forbids on purpose.

### The diagram, and what checks it

The header that declares a template carries its diagram directly under a line
naming the machine, one line per row, ending at the first empty comment line:

```c
/*
 * State machine: focus
 *   UNFOCUSED -> FOCUSED     event EVT_CLIENT_FOCUSED     guard focus_guard_can_focus
 *   FOCUSED   -> UNFOCUSED   event EVT_CLIENT_UNFOCUSED   guard focus_guard_can_unfocus
 *
 * Prose below the diagram is for the reader and is not checked.
 */
SMTemplate* focus_sm_template_create(void);
```

`tests/unit/test-sm-templates.c` reads that block out of each header and fails
when a declared state is missing from it or a declared row is not drawn. Rename
a state or add a row and the diagram has to follow in the same commit. What the
check cannot see is a line whose guard or event is wrong: that is what the
review of a template change is for (`.agents/skills/wm-review/SKILL.md`).

---

## State Machine Operations

The implementation is `src/sm/sm-instance.c`. The notes here are the parts a
sketch gets wrong; they are not a second copy of the functions.

### Create

`sm_create()` sets the state to the template's initial state, retains the
template, and counts that initial state as entered. `emit` may be NULL. A
transition then calls `hub_emit` with the row's event and `TARGET_ID_NONE`.

### Raw Write (for listeners/authoritative events)

`sm_raw_write()` sets the state with no guard. If the table has a row from the
old state to the new one, that row's event is emitted. A pair the table does
not declare emits nothing. The trace records a raw write with guard `none`.

### Transition (for requests)

`sm_transition()` looks up the row from the current state to the requested
state.

- No row: the trace records a refusal (`guard none`, not applied) and the call
  returns false. The state does not change.
- `SM_HOOK_PRE_GUARD` runs before the guard. A guard that returns false records
  `guard rejected` and returns false. It does not run the later hook phases.
- An action that returns false leaves the state unchanged. The trace says the
  guard passed only when the row named a guard.
- On success the state changes, the trace records the row's event, and that
  event is emitted. `SM_HOOK_POST_ACTION` runs after the state change and
  before the emit.

### Query Available Transitions

`sm_get_available_transitions()` returns the target states of rows that leave
the current state. The caller frees the array. `sm_can_transition()` reports
whether such a row exists. It does not run the guard.

---

## Built-in State Machines

These are the templates the tree ships. A name that is not in this table is
not a machine yet.

| SM | States | Lives on | Component |
|----|--------|----------|-----------|
| focus | UNFOCUSED, FOCUSED | Client | focus |
| fullscreen | WINDOWED, FULLSCREEN | Client | fullscreen |
| tag-view | EMPTY, TAGGED | Monitor | tag-manager |
| layout | TILE | Monitor | tiling |
| connection | DISCONNECTED, CONNECTED | no instance | connection-sm (template only) |

---

## Example: Fullscreen Component's SM Template

```
WINDOWED <-> FULLSCREEN
```

```c
static const SMStateName states[] = {
  { FULLSCREEN_STATE_WINDOWED,   "WINDOWED"   },
  { FULLSCREEN_STATE_FULLSCREEN, "FULLSCREEN" },
};

static const SMTransition rows[] = {
  { FULLSCREEN_STATE_WINDOWED,   FULLSCREEN_STATE_FULLSCREEN, "fs_guard_can_fullscreen",   "fs_action_enter_fullscreen", EVT_FULLSCREEN_ENTERED },
  { FULLSCREEN_STATE_FULLSCREEN, FULLSCREEN_STATE_WINDOWED,   "fs_guard_can_unfullscreen", "fs_action_exit_fullscreen",  EVT_FULLSCREEN_EXITED  },
};

static const SMTemplateDef def =
    SM_TEMPLATE_DEF("fullscreen", FULLSCREEN_STATE_WINDOWED, states, rows);

// Guards decide whether a request is allowed. The X changes are made by the
// component's executor, not here.
bool fs_guard_can_fullscreen(StateMachine* sm, void* data) {
  Client* c = (Client*) sm->owner;
  return c->managed;
}

bool fs_guard_can_unfullscreen(StateMachine* sm, void* data) {
  return true;
}
```

---

## Guards vs Actions

**Guards:** Validate whether a transition is allowed
- Run BEFORE the transition happens
- Can inspect current state, target state, owner properties
- Can reject transition (return false)

**Actions:** Side effects that happen during transition
- Run AFTER guard passes, BEFORE state update
- Usually empty (executor handles X calls)
- Can be used for pure in-SM effects (logging, updating other fields)

---

## Hooks (for Plugins)

`sm_add_hook()` attaches an observer to one `SMHookPhase` (`src/sm/sm-instance.h`).
Hooks may log or update a cache. A guard is the place that refuses a transition;
a hook is not a second guard.

The phases, in the order `sm_transition()` runs them on a successful change, are
`SM_HOOK_PRE_GUARD`, `SM_HOOK_POST_GUARD`, `SM_HOOK_PRE_ACTION`,
`SM_HOOK_POST_ACTION`, `SM_HOOK_PRE_EMIT`, `SM_HOOK_POST_EMIT`.

---

## Memory Management

A machine is stored on its target by name (`client_set_sm()`,
`monitor_set_sm()`), not as a field named after the machine. The component
creates it on first use and the target frees it in `client_destroy()` or
`monitor_destroy()`.

---

## Thread Safety

Each SM is single-threaded (one event at a time). For v1, all processing is single-threaded.

If multi-threaded in future:
- SM operations would need mutex (or per-SM locks)
- Event emission could be lock-free if using a ring buffer

---

## Header: sm.h

The declarations are in `src/sm/sm.h` and the files it includes; this document
does not keep a second copy, because two copies drift and the one in the header
is the one that compiles.

| File | Declares |
|------|----------|
| `src/sm/sm-instance.h` | `StateMachine`, `sm_create()`, `sm_destroy()`, `sm_raw_write()`, `sm_transition()`, `sm_can_transition()`, `sm_get_available_transitions()`, the hook API, `sm_get_state()`, `sm_get_name()` |
| `src/sm/sm-template.h` | `SMStateName`, `SMTransition`, `SMTemplateDef`, `SM_TEMPLATE_DEF()`, `sm_template_create()`, `sm_template_from_def()`, `sm_template_validate()`, `sm_template_destroy()` |
| `src/sm/sm-trace.h` | `SMTraceEntry`, `sm_trace_count()`, `sm_trace_snapshot()` |
| `src/sm/sm-debug.h` | `sm_debug_dump()` and the live-machine list |
| `src/sm/sm-registry.h` | registering and looking up guards and actions by name |

---

## Testing Strategy

1. **Unit tests for sm_transition**: Valid/invalid transitions
2. **Unit tests for guards**: Guard rejection/acceptance
3. **Unit tests for hooks**: Hooks called in correct order
4. **Unit tests for raw_write**: Forced state changes
5. **Integration with target**: SM lifecycle tied to target lifecycle

---

*See also:*
- `architecture/overview.md` — High-level architecture
- `architecture/component.md` — How components use SMs
- `architecture/target.md` — Target adoption of SM templates
