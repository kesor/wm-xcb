# Component Design

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-30*

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

The event loop in `wm-xcb.c` routes events to registered handlers:

```c
void handle_xcb_events() {
    xcb_generic_event_t* event = xcb_poll_for_event(dpy);
    if (!event) return;
    
    /* Handle errors (response_type = 0) */
    if (event->response_type == 0) {
        error_details((xcb_generic_error_t*)event);
        free(event);
        return;
    }
    
    /* Dispatch to registered handlers */
    xcb_handler_dispatch(event);
    
    /* Handle state events (e.g., running flag changes) */
    handle_state_event(event);
    
    free(event);
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
- `wm-xcb.c` — event loop integration (calls `xcb_handler_init()`, `xcb_handler_shutdown()`, `xcb_handler_dispatch()`)

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
2. Performs X server operations (via XCB)
3. On success: raw-writes to the target's state machine
4. On failure: emits an error event

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
    
    // Perform X operation (non-blocking)
    if (target_state == FS_FULLSCREEN) {
        xcb_void_cookie_t ck = xcb_ewmh_request_change_wm_state(
            ewmh,
            c->window,
            EWMH_WM_STATE_ADD,
            ewmh->_NET_WM_STATE_FULLSCREEN,
            XCB_ATOM_NONE,
            0
        );
        // Will handle reply/error later...
        sm_raw_write(sm, FS_FULLSCREEN);
    } else {
        xcb_ewmh_request_change_wm_state(...);
        sm_raw_write(sm, FS_WINDOWED);
    }
}
```

### Executor and XCB Error Handling

XCB operations are non-blocking. The executor should:
1. Send the request with a cookie
2. Store the correlation ID
3. Return immediately

A separate XCB reply handler will:
1. Check the cookie for success/failure
2. If success: sm_raw_write() to the SM
3. If failure: emit EVT_REQUEST_FAILED

```c
void fullscreen_executor(Request* req) {
    Client* c = hub_get_target(req->target);
    StateMachine* sm = client_get_sm(c, "fullscreen");
    uint32_t target_state = (sm_get_state(sm) == FS_FULLSCREEN) ? FS_WINDOWED : FS_FULLSCREEN;
    
    // Store pending request for later reply handling
    pending_request_t* pr = malloc(sizeof(pending_request_t));
    pr->correlation_id = req->correlation_id;
    pr->client_id = req->target;
    pr->target_state = target_state;
    pending_requests_add(pr);
    
    // Send XCB request
    if (target_state == FS_FULLSCREEN) {
        xcb_ewmh_request_change_wm_state(ewmh, c->window, EWMH_WM_STATE_ADD, ...);
    } else {
        xcb_ewmh_request_change_wm_state(ewmh, c->window, EWMH_WM_STATE_TOGGLE, ...);
    }
}

// Called when XCB reply comes back
void on_ewmh_reply(xcb_generic_reply_t* reply, void* data) {
    pending_request_t* pr = data;
    StateMachine* sm = client_get_sm(hub_get_target(pr->client_id), "fullscreen");
    
    if (reply) {
        sm_raw_write(sm, pr->target_state);
    } else {
        hub_emit(EVT_REQUEST_FAILED, pr->client_id, "fullscreen failed");
    }
    
    pending_requests_remove(pr);
}
```

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

## Hot-Plugging

Components can be added or removed at runtime.

### Register a new component
```c
void plugin_load(const char* name) {
    Component* c = load_component_from_file(name);
    hub_register_component(c);
    c->on_init();
    
    // Notify all compatible targets to adopt
    Target** targets = hub_get_targets_by_type(c->accepted_targets[0]);
    for (int i = 0; targets[i]; i++) {
        component_adopt(targets[i], c);
    }
}
```

### Unregister a component
```c
void plugin_unload(const char* name) {
    Component* c = hub_get_component(name);
    
    // Unadopt from all targets
    Target** targets = hub_get_all_targets();
    for (int i = 0; targets[i]; i++) {
        if (target_has_adopted(targets[i], c)) {
            component_unadopt(targets[i], c);
        }
    }
    
    c->on_shutdown();
    hub_unregister_component(name);
}
```

---

## Header: component.h

```c
#ifndef COMPONENT_H
#define COMPONENT_H

// Forward declarations
typedef struct Component Component;
typedef struct Target Target;
typedef struct Request Request;
typedef struct Event Event;
typedef struct SMTemplate SMTemplate;
typedef uint32_t TargetType;
typedef uint32_t RequestType;
typedef uint32_t EventType;

struct Component {
    const char* name;
    
    TargetType* accepted_targets;
    uint32_t num_targets;
    
    RequestType* requests;
    uint32_t num_requests;
    
    EventType* events;
    uint32_t num_events;
    
    // Lifecycle
    void (*on_init)(void);
    void (*on_shutdown)(void);
    void (*on_adopt)(Target* t);
    void (*on_unadopt)(Target* t);
    
    // Operations
    void (*executor)(Request* req);
    void (*listener)(Target* t, Event* e);
    
    // SM template
    SMTemplate* (*get_sm_template)(void);
    
    // Internal
    void* instance_data;
};

// Component lifecycle
void component_init(Component* c);
void component_destroy(Component* c);

// Target adoption
void component_adopt(Component* c, Target* t);
void component_unadopt(Component* c, Target* t);

// Query
bool component_handles_request(Component* c, RequestType type);
bool component_emits_event(Component* c, EventType type);
bool component_accepts_target(Component* c, TargetType type);

#endif // COMPONENT_H
```

---

## Testing Strategy

1. **Unit tests for each component**: Send requests, verify SM transitions
2. **Unit tests for listeners**: Send events, verify SM raw_write
3. **Integration tests**: Full flow request → executor → SM → event emission
4. **Hot-plug tests**: Load/unload component, verify adoption

---

*See also:*
- `architecture/overview.md` — High-level architecture
- `architecture/hub.md` — Hub routing
- `architecture/state-machine.md` — SM framework
- `architecture/target.md` — Target design and adoption
