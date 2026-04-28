# State Machine Framework

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-28*

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
    
    // Hooks for plugins (optional)
    Hook* pre_guards;      // executed before guard
    Hook* post_actions;    // executed after action
    
    // Instance-specific data (from template)
    void* data;
} StateMachine;
```

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

### Create (lazy)
```c
StateMachine* sm_create(Target* owner, SMTemplate* template) {
    StateMachine* sm = malloc(sizeof(StateMachine));
    sm->name = template->name;
    sm->owner = owner;
    sm->template = template;
    sm->current_state = template->initial_state;
    sm->pre_guards = NULL;
    sm->post_actions = NULL;
    sm->data = NULL;
    
    // Call template init function if any
    if (template->init_fn) {
        template->init_fn(sm);
    }
    
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
    Client* c = (Client*)sm->owner;
    // Can always fullscreen a managed client
    return c->managed;
}

bool fs_guard_can_unfullscreen(StateMachine* sm, void* data) {
    return true;  // Can always unfullscreen
}

// Action functions (for transitions triggered by requests)
void fs_action_enter_fullscreen(StateMachine* sm, void* data) {
    // Actions are handled by executor, not here
    // This is only for pure SM-side effects if needed
}

void fs_action_exit_fullscreen(StateMachine* sm, void* data) {
    // Actions are handled by executor, not here
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

**Lazy allocation:**
```c
StateMachine* client_get_sm(Client* c, const char* sm_name) {
    if (strcmp(sm_name, "fullscreen") == 0) {
        if (!c->sm_fullscreen) {
            c->sm_fullscreen = sm_create(c, &fullscreen_template);
        }
        return c->sm_fullscreen;
    }
    // ... other SMs
}
```

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

// State machine instance
struct StateMachine {
    const char* name;
    uint32_t current_state;
    Target* owner;
    
    struct SMTemplate* template;
    
    struct Hook* hooks[HOOK_MAX];
    void* data;
};

// Operations
StateMachine* sm_create(Target* owner, struct SMTemplate* tmpl);
void sm_destroy(StateMachine* sm);

void sm_raw_write(StateMachine* sm, uint32_t new_state);
bool sm_transition(StateMachine* sm, uint32_t target_state);
uint32_t sm_get_state(StateMachine* sm);

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
struct SMTemplate* sm_get_template(StateMachine* sm);
const char* sm_get_state_name(StateMachine* sm, uint32_t state);

#endif // SM_H
```

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
