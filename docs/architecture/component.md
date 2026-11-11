# Component Design

*Part of architecture documentation — Authoritative*
*Last updated: 2026-11-06*

> **💡 Paradigm Shift:** Components don't patch core code. They register with the Hub, provide state machine templates, and handle requests/events. Everything is additive.

---

## Purpose

A Component is a driver that can act upon targets. It is composed of:
- **Executor** — handles requests, interacts with X server
- **Listener** — handles external events, raw-writes to target's SM
- **SM Template** — defines state machine for targets to adopt

Components are the building blocks of the window manager. Built-in functionality (fullscreen, floating, focus) and extensions (gap, pertag, urgency) are all components.

**Key design principle:** Components do NOT own state machines. They provide templates that targets adopt.

---

## Component Interface

```c
struct Component {
    const char* name;           // unique identifier (for debugging/logging)
    
    // Target type filtering
    TargetType* accepted_targets;  // which target types this component works with
    uint32_t num_targets;
    
    // Request types this component handles
    RequestType* requests;
    uint32_t num_requests;
    
    // Event types this component emits
    EventType* events;
    uint32_t num_events;
    
    // Lifecycle hooks
    void (*on_init)(void);                    // called once at startup
    void (*on_shutdown)(void);                // called once at shutdown
    
    void (*on_adopt)(Target* t);              // called when target adopts this component
    void (*on_unadopt)(Target* t);            // called when target unadopts this component
    
    // Executor: handles requests
    void (*executor)(Request* req);
    
    // Listener: handles external events, raw-writes to SM
    void (*listener)(Target* t, Event* e);
    
    // SM template factory
    struct SMTemplate* (*get_sm_template)(void);
};
```

---

## XCB Handler Registration

Components register their own XCB event handlers. Raw X events go directly to components, NOT through the hub. The XCB handler infrastructure (`src/xcb/xcb-handler.c/h`) provides the registration and dispatch mechanism.

### Handler Registration API

```c
// Register a handler for an XCB event type
int xcb_handler_register(XCBEventType event_type, HubComponent* component, void (*handler)(void*));

// Lookup handlers for an event type
XCBHandler* xcb_handler_lookup(XCBEventType event_type);

// Get next handler in chain (for iterating multiple handlers)
XCBHandler* xcb_handler_next(XCBHandler* handler);

// Dispatch an event to all registered handlers
void xcb_handler_dispatch(void* event);

// Unregister all handlers for a component
void xcb_handler_unregister_component(HubComponent* component);
```

### Handler Registration at Init

```c
void keybinding_on_init(void) {
    xcb_handler_register(XCB_KEY_PRESS, &keybinding_component, keybinding_handler);
    xcb_handler_register(XCB_KEY_RELEASE, &keybinding_component, keybinding_handler);
}

void keybinding_handler(void* event) {
    xcb_key_press_event_t* e = (xcb_key_press_event_t*)event;
    // Look up keybinding, send request to hub
    hub_send_request(REQ_CLIENT_FOCUS, TARGET_CURRENT_CLIENT);
}
```

### XCB Infrastructure Dispatches Directly

The event loop in `src/core/wm-xcb.c` routes events to registered handlers. It waits in
`poll(2)` first, so the dispatch below only runs when there is something to
dispatch. `poll(2)` does not see an event libxcb has already read into its own
queue, so the wait takes that event first and the dispatch handles it before the
drain; `xcb-integration.md` has the full loop.

```c
void handle_xcb_events() {
    xcb_generic_event_t* event;

    if (queued_event != NULL) {
        event        = queued_event;
        queued_event = NULL;
        dispatch_event(event);
        free(event);
    }

    while ((event = xcb_poll_for_event(dpy)) != NULL) {
        dispatch_event(event);
        free(event);
    }

    xcb_flush(dpy);
}
```

### Event Type Mapping

| XCB Event | Handler Function |
|-----------|------------------|
| XCB_KEY_PRESS | keybinding_handler |
| XCB_KEY_RELEASE | keybinding_handler |
| XCB_BUTTON_PRESS | pointer_handler |
| XCB_BUTTON_RELEASE | pointer_handler |
| XCB_MOTION_NOTIFY | pointer_handler |
| XCB_ENTER_NOTIFY | focus_handler |
| XCB_FOCUS_IN | focus_handler |
| XCB_PROPERTY_NOTIFY | fullscreen_handler, urgency_handler |
| XCB_CREATE_NOTIFY | client_list_handler |
| XCB_DESTROY_NOTIFY | client_list_handler |
| XCB_MAP_REQUEST | client_list_handler |
| XCB_EXPOSE | bar_handler |

### XCB Handler Infrastructure Files

- `src/xcb/xcb-handler.c` — handler registration and dispatch implementation
- `src/xcb/xcb-handler.h` — public API and event type constants
- `src/core/wm-xcb.c` — event loop integration (calls `xcb_handler_init()`, `xcb_handler_shutdown()`, `xcb_handler_dispatch()`)

---

## What Belongs in a Component

### ✅ DO Put in Components

- **Executor**: Handles requests for this component's domain
- **Listener**: Handles XCB events and hub events relevant to this domain
- **SM Template**: Defines the state machine for targets to adopt
- **XCB Handler Registration**: Register handlers for events this component cares about

### ❌ DON'T Put in Components

| Anti-Pattern | Why It's Wrong | Correct Approach |
|-------------|----------------|------------------|
| Hardcoded keybindings | Components shouldn't know about each other's triggers | Keybinding component dispatches; config wires actions |
| SM instances | SMs belong on targets via adoption | Targets call `target_get_sm(t, "fullscreen")` |
| Other components' state | Violates single responsibility | Components communicate via hub events/requests |

**Examples:**

```c
// ❌ WRONG - Target hardcoding SMs
struct Client {
    Target target;
    StateMachine* fullscreen_sm;  // ❌ Hardcoded reference
};

// ✅ CORRECT - Generic SM lookup
StateMachine* sm = client_get_sm(c, "fullscreen");

// ❌ WRONG - Keybinding knowing about tags
void keybinding_init(void) {
    register_key(XK_1, tag_view, 1);  // ❌ Tags aren't keybinding's domain!
}

// ✅ CORRECT - Config wires key → action
void config_init(void) {
    keybinding_register("Mod+1", tag_manager.action("view", 1));
}
```

---

## Rules no gate checks

Two rules here are checked by review, not by a command. Each says why that
is, and what a violation looks like.

### A component does not reach another component

A component never includes another component's header and never calls its
functions. It sends a request through `hub_send_request()` and subscribes to
events through `hub_subscribe()`; the hub owns the registry, the router and the
bus. Reading a state machine another component stored on the same target is not
a violation: tiling reads the fullscreen and floating machines by name, and
`docs/PHILOSOPHY.md` §2.1 says why that stays inside the rule.

No gate reads the include lines under `src/components/` today, so review is the
check. Another component's header is written two ways, and a search for only
the first misses the second:

```bash
rg -n '#include "[^"]*components/' src/components/
rg -n '#include "[a-z0-9-]+\.h"' src/components/
```

A hit is a finding when the header belongs to another component. The including
file's own header, and another file of the same component, are not. The first
command does not match a bare name.

### A header says what a function does and who owns what it allocates

A function that stores a pointer, takes ownership of a buffer, or returns
allocated memory says so in the comment next to its declaration: what the
caller may free, what it must not, and what the function leaves allocated when
it fails. The implementation matches that sentence, and a change to either
updates both in the same commit.

No gate reads a comment. `scripts/check-compiler-refusals.sh` covers the part a
compiler can see — a machine-tracked field or a dropped registration result —
and the sanitizer gates catch a leak a test reaches. Ownership is prose, so a
reviewer is what checks it.

---

## Lifecycle

```
┌─────────────────────────────────────────────────────────────────────┐
│                         COMPONENT LIFECYCLE                         │
│                                                                     │
│  ┌────────┐                                                         │
│  │ INIT   │ ← hub_init() calls component->on_init() for all         │
│  │        │    and xcb_handler_register() for X events              │
│  └────┬───┘                                                         │
│       ▼                                                             │
│  ┌─────────┐     ┌─────────┐     ┌─────────┐     ┌─────────┐        │
│  │ ADOPTED │ ←── │ TARGET  │ ←── │ hub_    │ ←── │ EVENT   │        │
│  │         │     │ CREATED │     │ register│     │ LISTENER│        │
│  └─────┬───┘     └─────────┘     │ _target │     │ ADDED   │        │
│        │                         └─────────┘     └─────────┘        │
│        ▼                                                            │
│  ┌─────────┐                                                        │
│  │ ACTIVE  │ ← component handles requests/events for this target    │
│  └─────┬───┘                                                        │
│        │                                                            │
│        ▼                                                            │
│  ┌─────────┐     ┌──────────┐     ┌───────────────────┐             │
│  │UNADOPTED│ ←── │ TARGET   │ ←── │ hub_              │             │
│  │         │     │ DESTROYED│     │ unregister_target │             │
│  └─────┬───┘     └──────────┘     └───────────────────┘             │
│        │                                                            │
│        ▼                                                            │
│  ┌─────────┐                                                        │
│  │SHUTDOWN │ ← hub_shutdown() calls component->on_shutdown()        │
│  └─────────┘                                                        │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Executor

The executor handles requests. It:
1. Receives a request with a concrete target
2. Asks the target's state machine to transition
3. On success: the row's action performed the X server operation
4. On refusal or failure: emits an error event, and nothing reached the server

A request from a keybinding is intent, so it goes through `sm_transition()`,
not `sm_raw_write()`. The guard decides, the action does the X work the row
names, and a refused transition leaves the server and the machine alone. An
executor that writes the X property itself and then raw-writes the state skips
both the guard and the action: the row becomes decoration, and a client the
guard exists to refuse is changed anyway.

```c
void fullscreen_executor(Request* req) {
    // req->type == REQ_CLIENT_FULLSCREEN
    // req->target is concrete client ID

    Client* c = (Client*)hub_get_target(req->target);
    if (!c) {
        hub_emit(EVT_ERR_TARGET_NOT_FOUND, req->target, NULL);
        return;
    }

    // Get or create the SM (on-demand allocation)
    StateMachine* sm = client_get_sm(c, "fullscreen");

    // Determine target state based on current state
    uint32_t target_state = (sm_get_state(sm) == FS_FULLSCREEN)
        ? FS_WINDOWED
        : FS_FULLSCREEN;

    // The guard on the row runs first. When it passes, the row's action issues
    // the _NET_WM_STATE change and the machine moves; when it refuses, nothing
    // is written to the window.
    if (!sm_transition(sm, target_state)) {
        hub_emit(EVT_FULLSCREEN_FAILED, req->target, NULL);
    }
}
```

The listener still uses `sm_raw_write()`: an X property change the WM did not
ask for is reality, and reality is authoritative. A reply already in flight
when a local write moved the machine is not that change. The read stored the
machine's generation, and a reply from an older generation is freed and not
applied.

### Executor and XCB Error Handling

XCB operations are non-blocking. The fullscreen request returns a void cookie,
so there is no reply to store. The executor asks the machine to transition and
returns. The row's action sends the request. A refused transition emits
`EVT_FULLSCREEN_FAILED` and sends nothing, because the action did not run.

A request whose outcome arrives only in a reply is not this request. Sending it
before the guard has run changes the server for a client the guard would
refuse. That request transitions when the reply arrives. A refusal emits the
failure event and does not raw-write the state the server rejected. The action
on that path does not send the request again: the reply is the outcome of the
send, not a second send.

---

## Listener (Hub Events Only)

**Important distinction:**
- **XCB events** → component owns handler, called directly via `xcb_handler_register()`
- **Hub events (EVT_*)** → component subscribes via `hub_subscribe()`

The listener in the Component struct is for hub events, not raw XCB events:

```c
void bar_listener(Target* t, Event* e) {
    // Subscribed to hub events: EVT_TAG_CHANGED, EVT_LAYOUT_CHANGED, etc.
    if (e->type == EVT_TAG_CHANGED) {
        bar_redraw(t->data);
    }
}

void bar_on_init(void) {
    hub_subscribe(EVT_TAG_CHANGED, bar_listener, NULL);
}
```

### Two Types of Listeners

| Event Source | Mechanism | Example |
|-------------|-----------|----------|
| XCB raw events | `xcb_handler_register()` | keybinding_handler(KEY_PRESS) |
| Hub events | `hub_subscribe()` | bar_listener(EVT_TAG_CHANGED) |


---

## SM Template

The SM template defines what state machine the component provides. Targets adopt it and create their own SM instance.

```c
struct SMTemplate {
    const char* name;          // must be unique (e.g., "fullscreen", "floating")
    
    uint32_t* states;          // array of state enum values
    uint32_t num_states;
    
    Transition* transitions;    // array of valid transitions
    uint32_t num_transitions;
    
    uint32_t initial_state;    // default starting state
    
    // Optional init function for instance data
    void (*init_fn)(StateMachine* sm, void* instance_data);
    
    // Optional cleanup function
    void (*destroy_fn)(StateMachine* sm);
};

// Example: Fullscreen template
SMTemplate fullscreen_template = {
    .name = "fullscreen",
    .states = (uint32_t[]){ FS_WINDOWED, FS_FULLSCREEN },
    .num_states = 2,
    .transitions = fs_transitions,
    .num_transitions = 2,
    .initial_state = FS_WINDOWED,
    .init_fn = fs_sm_init,
    .destroy_fn = fs_sm_destroy,
};
```

---

## Component Example: Fullscreen

```c
// Component definition
Component fullscreen_component = {
    .name = "fullscreen",
    
    .accepted_targets = (TargetType[]){ TARGET_TYPE_CLIENT },
    .num_targets = 1,
    
    .requests = (RequestType[]){ REQ_CLIENT_FULLSCREEN },
    .num_requests = 1,
    
    .events = (EventType[]){
        EVT_FULLSCREEN_ENTERED,
        EVT_FULLSCREEN_EXITED,
        EVT_REQUEST_FAILED,
    },
    .num_events = 3,
    
    .on_init = fullscreen_init,
    .on_shutdown = fullscreen_shutdown,
    .on_adopt = fullscreen_on_adopt,
    .on_unadopt = fullscreen_on_unadopt,
    
    .executor = fullscreen_executor,
    .listener = fullscreen_listener,
    
    .get_sm_template = fullscreen_get_template,
};
```

### on_init
```c
void fullscreen_init(void) {
    // Subscribe to XCB PropertyNotify events
    hub_subscribe(EVT_XCB_PROPERTY_NOTIFY, fullscreen_xcb_handler, NULL);
    // Or: register with xcb_listener system
}
```

### on_adopt
```c
void fullscreen_on_adopt(Target* t) {
    // Nothing special needed here
    // Listener is already attached to target
    // SM is lazily created on first use
}
```

### get_sm_template
```c
SMTemplate* fullscreen_get_template(void) {
    return &fullscreen_template;
}
```

---

## Built-in Components

### Client Components

| Component | Target Type | Requests | Events | SM |
|-----------|------------|----------|--------|-----|
| fullscreen | CLIENT | REQ_CLIENT_FULLSCREEN | EVT_FULLSCREEN_* | FullscreenSM |
| floating | CLIENT | REQ_CLIENT_FLOAT, REQ_CLIENT_TILE | EVT_CLIENT_* | FloatingSM |
| urgency | CLIENT | REQ_CLIENT_CLEAR_URGENT | EVT_CLIENT_URGENT | UrgencySM |
| focus | CLIENT | REQ_CLIENT_FOCUS | EVT_CLIENT_FOCUSED | FocusSM |

### Monitor Components

| Component | Target Type | Requests | Events | SM |
|-----------|------------|----------|--------|-----|
| connection | MONITOR | (none - raw write only) | EVT_MONITOR_* | ConnectionSM |
| resolution | MONITOR | REQ_MONITOR_SET_RESOLUTION | EVT_RESOLUTION_* | ResolutionSM |

### Global Components

| Component | Target Type | Requests | Events | SM |
|-----------|------------|----------|--------|-----|
| keybinding | (none) | dispatches to wired actions | - | (none) |
| bar | MONITOR | REQ_BAR_UPDATE | (internal) | (none) |

> **Note:** The keybinding component should NOT hardcode bindings. See "Actions and Wiring" pending design.

---

## Plugin Components

Plugins register as components and can provide:
- Additional SM templates (new state to track)
- New request handlers
- New event listeners

Example: gap component
```c
Component gap_component = {
    .name = "gap",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_MONITOR },
    .requests = (RequestType[]){ REQ_MONITOR_SET_GAP },
    .events = (EventType[]){ EVT_GAP_CHANGED },
    .get_sm_template = gap_get_template,
    // No executor - gap affects tiling, which is a different component
    // This component just provides the GapSM
};
```

---

## Adding a Component at Runtime

The built-in components are compiled in and registered from `wm_components_init()` in `src/core/wm-components.c`. Clients are created after that function returns. Monitors are created inside it, by `monitor_manager_init()`. Pertag, tag-manager, and tiling register before that call. Monitor-manager accepts monitors and registers at the start of that call, before the monitors are created. The hub has no hot-plug path beyond the registry calls:

- `hub_register_component()` adds a component to the registry and the request index. It does not adopt targets that already exist. A component registered later is adopted by the targets created after it, through `hub_adopt_components_for_target()`. A component whose `on_adopt` does the work, and that must cover targets that already exist, walks `hub_get_targets_by_type_name()` and calls that hook on each. A component that creates its machine on first lookup does not get the machine from the hook.
- `hub_unregister_component()` removes the component from the registry and the request index. It does not call `on_unadopt` on targets, so a machine the component already stored stays on them.

There is no `hub_notify_compatible_targets()`, no adopted-components list on a target, and no loader: components are compiled in and registered from `src/core/wm-components.c`. See [hub.md](hub.md#component-adoption).

---

## Header: src/core/wm-hub.h

`HubComponent` is declared in `src/core/wm-hub.h`, together with the registration, lookup and unregistration calls. This document does not copy the header: a second copy drifts from the one the compiler reads.

---

## Testing Strategy

1. **Unit tests for each component**: Send requests, verify SM transitions
2. **Unit tests for listeners**: Send events, verify SM raw_write
3. **Integration tests**: Full flow request → executor → SM → event emission
4. **Adoption tests**: Register a component, create a target, verify the component's `on_adopt` hook received it; register the component after the target and verify it did not.

---

*See also:*
- `architecture/overview.md` — High-level architecture
- `architecture/hub.md` — Hub routing
- `architecture/state-machine.md` — SM framework
- `architecture/target.md` — Target design and adoption
