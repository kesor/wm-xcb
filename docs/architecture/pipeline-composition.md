# Pipeline Composition Architecture

*Part of architecture documentation — Authoritative*
*Last updated: 2026-05-01*

> **Design Session:** This document captures the design decisions from the 2026-05-01 session on issue #94.

---

## Purpose

Components expose capabilities as **actions**. Actions can be composed into **pipelines** in configuration, allowing complex workflows without coupling components to each other in code.

Example: "switch focus to next client"
```
focus.get-current → tiling.get-next → focus.set-current
```

This pipeline:
1. Gets the currently focused client ID
2. Looks up the next client in tiling order
3. Sets focus to that client

Components don't know about each other. Wiring is purely declarative.

---

## Pipeline Model

### Action Signature

```c
typedef bool (*ActionCallback)(ActionInvocation* inv);

struct ActionInvocation {
    void*   data;           // pipeline data — IS the target, input OR output
    void*   args;          // action-specific arguments (e.g., tag number, direction)
    uint64_t correlation_id;// for async response tracking
};

struct Action {
    const char*          name;            // unique name, e.g., "focus.get-current"
    const char*         description;     // human-readable description
    ActionCallback       callback;        // the action function
    ActionTargetResolver target_resolver; // how to resolve target
    void*                userdata;        // component-specific data
};
```

**Key insight:** `data` IS the target. It's both input and output as it flows through the pipeline. `args` is extra configuration per call.

### Pipeline Rules

| Position | data (input) | data (output) | Example |
|----------|--------------|---------------|---------|
| First | NULL (ignored) | Produces target | `focus.get-current` |
| Middle | Receives from previous | Produces new target | `tiling.get-next` |
| Last | Receives from previous | Ignored | `focus.set-current` |

### First Action

- Takes no input (data = NULL, ignored)
- Returns a target (sets data)
- Example: `focus.get-current` returns ClientID → data = ClientID

### Middle Action

- Receives target from previous action (data = previous output)
- Returns a new target (sets data)
- Example: `tiling.get-next` receives ClientID, returns next ClientID

### Last Action

- Receives target from previous action (data = previous output)
- Return value (bool) indicates success/failure, data is ignored
- Example: `focus.set-current` receives ClientID, sets focus

---

## Composition Syntax

### Wire Macro

```c
// Wire a pipeline of actions
WIRE("focus.get-current", "tiling.get-next", "focus.set-current")

// Or with named alias
ACTION("focus-cycle-next", "focus.get-current", "tiling.get-next", "focus.set-current")
```

### Implementation

```c
typedef struct Wire {
    const char** actions;  // NULL-terminated array of action names
    uint32_t     count;    // number of actions
} Wire;

typedef struct ActionDef {
    const char* name;           // "focus-cycle-next"
    const char** pipeline;      // {"focus.get-current", "tiling.get-next", "focus.set-current"}
} ActionDef;

// Or as a single string with arrow notation
const char* focus_cycle_next = "focus.get-current → tiling.get-next → focus.set-current";
```

---

## Component Responsibilities

### FocusComponent

**Owns:** `focused-client` target

**Provides actions:**
- `focus.get-current` — no input, returns ClientID (sets data = ClientID)
- `focus.set-current` — receives ClientID in data, returns bool
- `focus.get-client` — no input, returns Client* (convenience)

**Target:**
```c
Target focused_client = {
    .name = "focused-client",
    .data = NULL,  // points to current Client or NULL
};
```

### TilingComponent

**Owns:** Client list with layout order

**Provides actions:**
- `tiling.get-next` — receives ClientID in data, returns next ClientID (sets data = next)
- `tiling.get-prev` — receives ClientID in data, returns previous ClientID
- `tiling.get-first` — no input, returns first ClientID in master area
- `tiling.get-last` — no input, returns last ClientID in stack

### MonitorManagerComponent

**Owns:** `focused-monitor` target, monitor list

**Provides actions:**
- `monitor.get-current` — no input, returns MonitorID
- `monitor.set-current` — receives MonitorID in data, returns bool
- `monitor.get-next` — receives MonitorID in data, returns next MonitorID
- `monitor.get-by-direction` — receives direction enum in args, returns MonitorID

---

## Wiring Examples

### "Switch focus to next client"

```c
WIRE("focus.get-current", "tiling.get-next", "focus.set-current")
```

### "Move current client to next tag"

```c
WIRE("focus.get-current", "tag.get-current-monitor", "tag.move-client-to", "tag.refresh")
```

### "Focus client to the right"

```c
WIRE("focus.get-current", "client.get-relative", "focus.set-current")
// Where "client.get-relative" takes direction in args
```

### "Swap current client with master"

```c
WIRE("focus.get-current", "tiling.swap-with-master", "tiling.refresh")
```

---

## Target Model (vs Symbolic References)

### Old Model (Rejected)

```
CURRENT_CLIENT is a symbolic reference
→ resolved to actual Client at invocation
→ no target, no lifetime, no SM
```

### New Model (Accepted)

```
FocusedClient is a target owned by FocusComponent
- Exists at startup, always registered
- Its data pointer points to current Client or NULL
- Has no SM (it's a reference container, not a real entity)
```

**Rationale:**
- Clear ownership (FocusComponent owns it)
- Extensible (can add properties later if needed)
- Consistent with other targets

---

## Pipeline Execution

```c
bool wire_execute(const char** actions, uint32_t count) {
    void* data = NULL;  // pipeline flows through data
    
    for (uint32_t i = 0; i < count; i++) {
        Action* action = action_lookup(actions[i]);
        if (!action) {
            LOG_ERROR("Action not found: %s", actions[i]);
            return false;
        }
        
        ActionInvocation inv = {
            .data = data,  // input from previous (NULL for first)
            .args = NULL,
        };
        
        bool result = action->callback(&inv);
        data = inv.data;  // output becomes next input
        
        if (!result && i < count - 1) {
            LOG_WARN("Pipeline broken at action %s", actions[i]);
            return false;
        }
    }
    
    return true;
}
```

### Argument Passing

For actions that need arguments (e.g., tag number):

```c
// Wire with arg: Mod+1 → tag view 1
WIRE_ARG("tag.get-monitor", 1, "tag.set-view")

// Or via args field in invocation:
ActionInvocation inv = {
    .data = monitor,
    .args = (void*)(uintptr_t)1,  // tag number as argument
};
```

- `data` flows through the pipeline (the target being operated on)
- `args` is extra configuration for the specific action call

---

## Related Design

- **#94** (this doc) — Pipeline composition for actions
- **#99** — Dynamic target type registration (targets like "focused-client")

---

## Status

Design complete — implementation pending #99