# State Machine Framework

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-30*

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

```c
typedef struct StateMachine {
    const char* name;
    
    // Current state
    uint32_t current_state;
    
    // Owner
    Target* owner;  // the client/monitor this SM belongs to
    
    // Template reference
    SMTemplate* template;
    
    // Hooks for components (optional)
    Hook* pre_guards;      // executed before guard
    Hook* post_actions;    // executed after action
    
    // Instance-specific data (from template)
    void* data;
} StateMachine;
```

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

---

## State Machine Operations

### Create
```c
StateMachine* sm_create(void* owner, SMTemplate* template,
                        EventEmitter emit, void* emit_userdata) {
    StateMachine* sm = malloc(sizeof(StateMachine));
    sm->name = template->name;
    sm->owner = owner;
    sm->template = template;
    sm->current_state = template->initial_state;
    sm->data = NULL;

    // Emit callback is required - owner must connect SM to hub
    sm->emit = emit;

    return sm;
}
```

### Raw Write (for listeners/authoritative events)
```c
// Used by listeners when reality changes
void sm_raw_write(StateMachine* sm, uint32_t new_state) {
    // No guards - reality is authoritative
    sm->current_state = new_state;
    event_emit(sm->template->get_transition_event(sm, new_state));
}
```

### Transition (for requests)
```c
bool sm_transition(StateMachine* sm, uint32_t target_state) {
    // Find transition from current_state → target_state
    Transition* t = sm_find_transition(sm, sm->current_state, target_state);
    if (!t) return false;  // no valid transition
    
    // Run guards (if any)
    if (t->guard_fn && !sm_run_guard(sm, t->guard_fn)) {
        return false;  // guard rejected
    }
    
    // Run pre-hooks
    sm_run_hooks(sm, HOOK_PRE_GUARD);
    
    // Execute action
    if (t->action_fn) {
        sm_run_action(sm, t->action_fn);
    }
    
    // Update state
    sm->current_state = target_state;
    
    // Run post-hooks
    sm_run_hooks(sm, HOOK_POST_ACTION);
    
    // Emit event
    event_emit(t->emit_on_transition, sm->owner->id, sm->data);
    
    return true;
}
```

### Query Available Transitions
```c
// For UI/debugging: what can I request from here?
uint32_t* sm_get_available_transitions(StateMachine* sm, uint32_t* count) {
    *count = 0;
    uint32_t* states = malloc(sizeof(uint32_t) * sm->template->num_transitions);
    
    for (uint32_t i = 0; i < sm->template->num_transitions; i++) {
        if (sm->template->transitions[i].from_state == sm->current_state) {
            states[(*count)++] = sm->template->transitions[i].to_state;
        }
    }
    
    return states;  // caller frees
}

// Check if a specific transition is valid
bool sm_can_transition(StateMachine* sm, uint32_t target_state) {
    // Check guard (without running it)
    // Return whether transition exists and would pass guard
}
```

---

## Built-in State Machines

### Client State Machines

| SM | States | Component |
|----|--------|-----------|
| FullscreenSM | WINDOWED ↔ FULLSCREEN | fullscreen |
| FloatingSM | TILED ↔ FLOATING | floating |
| UrgencySM | CALM ↔ URGENT | urgency |
| FocusSM | UNFOCUSED ↔ FOCUSED | focus (managed clients only) |

### Monitor State Machines

| SM | States | Component |
|----|--------|-----------|
| ConnectionSM | CONNECTED ↔ DISCONNECTED | connection |
| ResolutionSM | RES_* (one per mode) | resolution |
| SelectedSM | SEL_1 ↔ SEL_N (selected monitor) | (system, no component) |

### Global State Machines

| SM | States | Component |
|----|--------|-----------|
| TagViewSM | TAGS_* (bitmask of visible tags) | (system) |
| LayoutSM | LAYOUT_* (current layout per monitor) | (system) |

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

Plugins can attach hooks to run before/after SM operations:

```c
typedef enum HookPhase {
    HOOK_PRE_GUARD,
    HOOK_POST_GUARD,
    HOOK_PRE_ACTION,
    HOOK_POST_ACTION,
    HOOK_PRE_EMIT,
    HOOK_POST_EMIT,
} HookPhase;

typedef void (*HookFn)(StateMachine* sm, void* userdata);

void sm_add_hook(StateMachine* sm, HookPhase phase, HookFn fn, void* userdata) {
    // Add to sm->hooks[phase]
}
```

**Use cases:**
- Plugin wants to log all fullscreen transitions
- Plugin wants to add a custom guard (e.g., "don't fullscreen if video is playing")
- Plugin wants to trigger UI update on state change

---

## Memory Management

**SM lives on target:**
```c
struct Client {
    // ... properties ...
    
    StateMachine* sm_fullscreen;
    StateMachine* sm_floating;
    StateMachine* sm_urgency;
    // ...
};
```

**On-demand allocation:**

Targets decide when to allocate SMs. Call `sm_create()` when the SM is first needed (e.g., on first request), not at target creation time. The SM stays allocated for the lifetime of the target.

**Target destruction:**
```c
void client_destroy(Client* c) {
    sm_destroy(c->sm_fullscreen);
    sm_destroy(c->sm_floating);
    sm_destroy(c->sm_urgency);
    // ... free other resources
    free(c);
}
```

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
