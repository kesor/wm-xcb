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

`StateMachine` is an opaque type in `sm-instance.h`. Its definition is in
`src/sm/sm-instance-internal.h`, which only `src/sm/sm-instance.c` includes.
`current_state` is the value the
single-writer rule is about: while the definition sat in the public header any
component could assign it and compile, and the WM and the X server would then
disagree days later with no trace back to the write. A translation unit that
never sees the definition cannot name the field, so that mistake is a build
error rather than a review finding.

What a component can read, it reads through the accessors:

| Value | Read with | Changed with |
|-------|-----------|--------------|
| current state | `sm_get_state()` | `sm_raw_write()`, `sm_transition()` |
| owner target | `sm_get_owner()` | fixed at `sm_create()` |
| template | `sm_get_template()` | fixed at `sm_create()` |
| instance data | `sm_get_data()` | `sm_set_data()`, `sm_set_owned_data()` |
| name | `sm_get_name()` | from the template |

A guard or an action that needs a value the accessors do not expose is a reason
to add an accessor, not to include the internal header from a component.

---

## Values a component cannot write

Each of these is tracked state: the WM's behaviour depends on the value, and the
value has mutually exclusive states. A component cannot assign any of them,
because the definition is in a header no component includes.

| Value | Written by | How the field is kept out of reach |
|-------|-----------|-----------------------------------|
| `StateMachine.current_state` | `sm_raw_write()`, `sm_transition()` | `sm-instance.h` declares the type only; the definition is in `sm-instance-internal.h` |
| `ClientState.managed`, `.urgent`, `.mapped` | `client_set_managed()`, `client_set_urgent()`, `client_set_mapped()` | `client.h` declares the type and a pointer to it; the definition is in `client.c` |

`scripts/check-private-state.sh` compiles every fixture under `tests/compile-fail/`
twice: with `-DPRIVATE_STATE_CONTROL` through the accessor, which must succeed, and
without it assigning the field, which must not compile. The control build is what
tells a refusal from a fixture that fails because an include path moved. The
fixtures include the public headers only. A file that includes
`sm-instance-internal.h` can assign `current_state`. A file that includes
`client.h` can assign `Client.tracked` itself, and swapping two clients' pointers
exchanges managed, urgent and mapped without an accessor. Neither assignment is
a fixture, so neither is what the gate refuses.

The tracked values that are still public, and why:

- **`struct Context.state`** in `src/core/wm-states.h` is the pre-hub transition table's
  state. Only `src/core/wm-states.c` and its test write it, and the table is due for
  removal in favour of the component that owns pointer input, so it does not earn
  an accessor layer first. wm-46k.15 tracks that subsystem.
- **`Client.tags`** is the set of tags the client is assigned to, not exclusive
  state, and it is written through `client_set_tags()`, `client_add_tag()` and
  `client_remove_tag()`.
- **`Client.focusable`, `Client.monitor` and `Client.stack_mode`** are a
  capability from WM_HINTS, an association, and a mirror of the X stack order.
- **`Client.urgent`** is behind an accessor but has no machine, even though the
  built-in list below still promises an UrgencySM. wm-46k.17 decides which of the
  two is wrong.
- **`selected_monitor`** is an `extern` in `monitor.h`. Only `monitor.c` assigns
  it; other files use `monitor_get_selected()` and `monitor_set_selected()`. It is
  which monitor is selected. The built-in table names a SelectedSM, and this
  pointer is the store that exists. It is not a field of `StateMachine` or
  `Client`, so making those opaque does not reach it.
- **`FocusComponent.focused_window`** is a field of the public component struct in
  `focus.h`. Only `focus.c` assigns it; readers use `focus_get_focused_window()`.
  It is a second copy of the client's focus machine: `current_state` is
  unreachable, and this window id is not. Taking the field out of the public
  struct is a change to that component, and it is not this one.
- **`Pertag.curtag` and `prevtag`** are fields of `struct Pertag`, which `pertag.h`
  publishes and `pertag_get_data()` returns, so a caller can assign them. No file
  outside `pertag.c` does. The live tag view is TagViewSM; this index is a second
  store the running WM does not read. Making `Pertag` incomplete would hide the
  write and would also hide the per-tag arrays the header exists to expose.

The client's fullscreen, floating and focus machines, the monitor's layout and
tag view, and the connection template are state machine instances, so the row for
`StateMachine.current_state` covers their state. It does not cover the copies
named above.

---

## SM Template Structure

```c
typedef struct SMTemplate {
    const char* name;
    
    // States
    uint32_t* states;
    uint32_t num_states;
    
    // Transitions: from → to with guard + action
    Transition {
        uint32_t from_state;
        uint32_t to_state;
        const char* guard_fn;   // name of guard function
        const char* action_fn;  // name of action function
        EventType emit_on_transition;
    }* transitions;
    uint32_t num_transitions;
    
    // Initial state (default)
    uint32_t initial_state;
} SMTemplate;
```

---

## State Machine Operations

### Create
```c
StateMachine* sm_create(void* owner, SMTemplate* template, EventEmitter emit) {
    StateMachine* sm = malloc(sizeof(StateMachine));
    sm->name = template->name;
    sm->owner = owner;
    sm->template = template;
    sm->current_state = template->initial_state;
    sm->data = NULL;
    sm->data_free = NULL;

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

```c
// States
enum {
    FS_WINDOWED = 0,
    FS_FULLSCREEN,
};

// Transitions
static Transition fs_transitions[] = {
    {
        .from = FS_WINDOWED,
        .to = FS_FULLSCREEN,
        .guard_fn = "fs_guard_can_fullscreen",
        .action_fn = "fs_action_enter_fullscreen",
        .emit = EVT_FULLSCREEN_ENTERED,
    },
    {
        .from = FS_FULLSCREEN,
        .to = FS_WINDOWED,
        .guard_fn = "fs_guard_can_unfullscreen",
        .action_fn = "fs_action_exit_fullscreen",
        .emit = EVT_FULLSCREEN_EXITED,
    },
};

// Template
SMTemplate fullscreen_template = {
    .name = "client-fullscreen",
    .states = (uint32_t[]){ FS_WINDOWED, FS_FULLSCREEN },
    .num_states = 2,
    .transitions = fs_transitions,
    .num_transitions = 2,
    .initial_state = FS_WINDOWED,
};

// Guard functions
bool fs_guard_can_fullscreen(StateMachine* sm, void* data) {
    Client* c = (Client*)sm_get_owner(sm);
    // Can always fullscreen a managed client
    return client_is_managed(c);
}

bool fs_guard_can_unfullscreen(StateMachine* sm, void* data) {
    return true;  // Can always unfullscreen
}

// Action functions (for transitions triggered by requests).
// A guard that refused never calls these. The executor does not issue the
// X request itself.
bool fs_action_enter_fullscreen(StateMachine* sm, void* data) {
    // Issues the X request the row names.
    return true;
}

bool fs_action_exit_fullscreen(StateMachine* sm, void* data) {
    // Removes what the enter action added.
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
- May issue the X request the row names; a refused transition does not reach them
- Must not change another state machine

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

```c
#ifndef SM_H
#define SM_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct StateMachine StateMachine;
typedef struct Target Target;
typedef struct Event Event;

// State machine instance: opaque, defined in sm-instance-internal.h
typedef struct StateMachine StateMachine;

// Operations
StateMachine* sm_create(void* owner, struct SMTemplate* tmpl, EventEmitter emit, void* emit_userdata);
void sm_destroy(StateMachine* sm);

void sm_raw_write(StateMachine* sm, uint32_t new_state);
bool sm_transition(StateMachine* sm, uint32_t target_state);
uint32_t sm_get_state(StateMachine* sm);

// Read the owner, the template, the instance data and the name
void* sm_get_owner(StateMachine* sm);
struct SMTemplate* sm_get_template(StateMachine* sm);
void* sm_get_data(StateMachine* sm);
const char* sm_get_name(StateMachine* sm);

// Query
bool sm_can_transition(StateMachine* sm, uint32_t target_state);
uint32_t* sm_get_available_transitions(StateMachine* sm, uint32_t* count);

// Hooks
typedef enum HookPhase {
    HOOK_PRE_GUARD,
    HOOK_POST_GUARD,
    HOOK_PRE_ACTION,
    HOOK_POST_ACTION,
    HOOK_PRE_EMIT,
    HOOK_POST_EMIT,
    HOOK_MAX,
} HookPhase;

typedef void (*HookFn)(StateMachine* sm, void* userdata);
void sm_add_hook(StateMachine* sm, HookPhase phase, HookFn fn, void* userdata);
void sm_remove_hook(StateMachine* sm, HookPhase phase, HookFn fn);

// Template helpers
const char* sm_get_state_name(StateMachine* sm, uint32_t state);

#endif // SM_H
```

---

## The pre-hub state machine in `src/core/wm-states.c`

`src/core/wm-states.c` predates this framework and is still linked into the WM. It keeps
one global `struct Context` for the whole display. `dispatch_event()` in
`src/core/wm-xcb.c` calls `handle_state_event()` after the handler registry for every
event that is not an X error. A key or button event is matched against
`transitions[]` in `src/config/config.def.h`, and the function of the current state is
called.

No row matches. `window_target_type` is set only from `EnterNotify`, and that
event returns before the table is consulted, so a button or key event has
`WndTypeNone` and every row requires a client or the root. The `Shift+Button1`
and `Alt+Button1` rows therefore do not change state. The actions are stubs
either way: `state_window_move()` and `state_window_resize()` log and return.
Pointer move and resize are not implemented. Behaviour belongs in a component's
state machine, not in that table; the table stays until the component that owns
pointer input exists.

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
