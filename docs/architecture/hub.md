# Hub Design

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-30*

> **💡 Paradigm Shift:** Components never talk to each other directly. They always go through the Hub. This decouples everything and enables true hot-plugging.

---

## Purpose

The Hub is the central orchestrator that connects all components and targets. It provides:
- Component registration and discovery
- Target registration and tracking
- Request routing
- Event distribution
- Target resolution

Components do not call each other directly. They communicate exclusively through the Hub.

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                              HUB                         │
│                                                          │
│  ┌──────────────┐  ┌───────────────┐  ┌───────────────┐  │
│  │  REGISTRY    │  │   ROUTER      │  │  EVENT BUS    │  │
│  │              │  │               │  │               │  │
│  │ components[] │  │ request →     │  │ subscribe()   │  │
│  │ targets[]    │  │ component     │  │ emit()        │  │
│  │              │  │               │  │               │  │
│  │ by_name      │  │ by_request_   │  │ by_event_     │  │
│  │ by_type      │  │ type          │  │ type          │  │
│  │              │  │               │  │               │  │
│  │ by_target    │  │ by_target_    │  │               │  │
│  │              │  │ id            │  │               │  │
│  └──────────────┘  └───────────────┘  └───────────────┘  │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

---

## Registry

The registry maintains:
1. All registered components
2. All created targets
3. Mappings between them

### Component Registration

```c
// Components register themselves at startup
void hub_register_component(Component* comp) {
    // Add to components[]
    // Create index: by_name[comp->name] = comp
    // Create index: by_request_type[comp->requests[i]] = comp
}

// Query: which component handles this request type?
Component* hub_get_component_for_request(RequestType type) {
    return by_request_type[type];
}
```

### Target Registration

```c
// When a target (client, monitor) is created, it's registered
void hub_register_target(Target* t) {
    // Add to targets[]
    // Create index: by_target_id[t->id] = t
    // Create index: by_target_type[t->type] = t[]
}

// Query: get target by ID
Target* hub_get_target(TargetID id) {
    return by_target_id[id];
}
```

### Compatible Components for Target Type

```c
// When target is created, it needs to adopt compatible components
Component** hub_get_components_for_target_type(TargetType type) {
    // Iterate components[], filter by accepted_targets[]
    // Return array of compatible components
}
```

---

## Router

The router directs requests to the appropriate component.

### Request Structure

```c
struct Request {
    RequestType type;
    TargetID target;
    void* userdata;        // for callbacks
    uint64_t correlation_id;  // for async responses
};

enum RequestType {
    REQ_CLIENT_FULLSCREEN,
    REQ_CLIENT_TILE,
    REQ_CLIENT_FLOAT,
    REQ_CLIENT_CLOSE,
    REQ_CLIENT_FOCUS,
    
    REQ_MONITOR_SET_LAYOUT,
    REQ_MONITOR_TAG_VIEW,
    REQ_MONITOR_TOGGLE_TAG,
    
    // ... more request types
};
```

### Routing Logic

```c
void hub_send_request(Request* req) {
    // 1. Resolve target if symbolic
    req->target = hub_resolve_target(req->target);
    
    // 2. Find component that handles this request type
    Component* comp = by_request_type[req->type];
    if (!comp) {
        // No component handles this - emit error event
        event_emit(EVT_ERR_UNHANDLED_REQUEST, req);
        return;
    }
    
    // 3. Dispatch to component's executor
    comp->executor(req);
}
```

### Target Resolution

```c
TargetID hub_resolve_target(TargetID symbolic) {
    switch (symbolic) {
        case TARGET_CURRENT_CLIENT:
            return focus_component_get_current_client();
        case TARGET_CURRENT_MONITOR:
            return monitor_component_get_current_monitor();
        case TARGET_ALL_CLIENTS:
        case TARGET_ALL_MONITORS:
            return symbolic;  // special markers, handled by executor
        default:
            return symbolic;  // already concrete
    }
}
```

### Special Target Handling

```c
void hub_broadcast_request(RequestType type, void* data) {
    // For TARGET_ALL_CLIENTS or similar
    // Find all targets of relevant type
    // Route request to each
}
```

---

## Event Bus

The event bus handles publish/subscribe for events.

### Event Structure

```c
struct Event {
    EventType type;
    TargetID target;     // which target this event is about
    void* data;         // event-specific payload
    uint64_t correlation_id;  // for matching requests to responses
};

enum EventType {
    // State machine transition events
    EVT_FULLSCREEN_ENTERED,
    EVT_FULLSCREEN_EXITED,
    EVT_CLIENT_TILED,
    EVT_CLIENT_FLOATING,
    
    // Monitor events
    EVT_MONITOR_CONNECTED,
    EVT_MONITOR_DISCONNECTED,
    EVT_RESOLUTION_CHANGED,
    
    // Client lifecycle
    EVT_CLIENT_MANAGED,
    EVT_CLIENT_UNMANAGED,
    EVT_CLIENT_FOCUSED,
    EVT_CLIENT_UNFOCUSED,
    
    // Request responses
    EVT_REQUEST_SUCCESS,
    EVT_REQUEST_FAILED,
    
    // ... more event types
};
```

### Subscription API

```c
// Subscribe to an event type
typedef void (*EventHandler)(Event* e);
void hub_subscribe(EventType type, EventHandler handler, void* userdata);

// Unsubscribe
void hub_unsubscribe(EventType type, EventHandler handler);

// Subscribe to events for a specific target
void hub_subscribe_target(EventType type, TargetID target, EventHandler handler, void* userdata);
```

### Event Emission

```c
void event_emit(Event* e) {
    // 1. Find all subscribers for this event type
    // 2. For each subscriber:
    //    - If subscribed to specific target, check target match
    //    - Call handler(e, userdata)
}

// Convenience: emit with type and target
void hub_emit(EventType type, TargetID target, void* data) {
    Event e = { .type = type, .target = target, .data = data };
    event_emit(&e);
}
```

### Correlation for Request/Response

```c
void hub_send_request_with_correlation(Request* req, uint64_t corr_id) {
    req->correlation_id = corr_id;
    
    // Requester should have subscribed to EVT_REQUEST_* with this corr_id
    hub_subscribe(EVT_REQUEST_SUCCESS, on_request_done, req->userdata);
    hub_subscribe(EVT_REQUEST_FAILED, on_request_done, req->userdata);
    
    hub_send_request(req);
}

void on_request_done(Event* e) {
    if (e->correlation_id != my_corr_id) return;
    // Handle response
}
```

---

## Built-in Target Types

```c
enum TargetType {
    TARGET_TYPE_CLIENT,
    TARGET_TYPE_MONITOR,
    TARGET_TYPE_KEYBOARD,
    TARGET_TYPE_TAG,
    TARGET_TYPE_SYSTEM,  // global WM state
};
```

---

## Target Resolution Components

Some components "own" the concept of certain targets and can resolve them:

```c
// FocusComponent owns TARGET_CURRENT_CLIENT
TargetID focus_component_resolve(TargetSymbolic sym) {
    if (sym == TARGET_CURRENT_CLIENT) {
        return focus_sm->current_client_id;
    }
    return INVALID_TARGET;
}
```

The hub stores a list of "target resolvers" and queries them in order.

```c
typedef TargetID (*TargetResolver)(TargetSymbolic sym);
TargetResolver resolvers[] = {
    focus_component_resolve,
    monitor_component_resolve,
    // ...
};

TargetID hub_resolve_target(TargetSymbolic sym) {
    for (auto resolver : resolvers) {
        TargetID result = resolver(sym);
        if (result != INVALID_TARGET) return result;
    }
    return sym;  // if no resolver matched, treat as concrete
}
```

---

## Error Handling

```c
// Error events that the hub can emit
enum EventType {
    // ... existing events ...
    
    EVT_ERR_UNHANDLED_REQUEST,   // no component handles this request type
    EVT_ERR_TARGET_NOT_FOUND,    // target ID doesn't exist
    EVT_ERR_NO_CURRENT_CLIENT,  // tried to resolve but no focused client
    EVT_ERR_NO_CURRENT_MONITOR, // tried to resolve but no selected monitor
};
```

Components can subscribe to error events to handle them (e.g., show error in bar).

---

## Thread Safety Considerations

For v1, single-threaded. XCB is async, but we're not doing parallel event processing.

If multi-threaded in future:
- Event bus needs mutex
- Registry needs mutex (or RCU)
- Each target's SM is single-threaded (one event at a time)

---

## Implementation Notes

### Header: hub.h

```c
#ifndef HUB_H
#define HUB_H

// Forward declarations
typedef uint64_t TargetID;
typedef uint32_t TargetType;
typedef uint32_t RequestType;
typedef uint32_t EventType;
typedef struct Component Component;
typedef struct Target Target;
typedef struct Request Request;
typedef struct Event Event;

// Hub initialization
void hub_init(void);
void hub_shutdown(void);

// Component registration
void hub_register_component(Component* comp);
void hub_unregister_component(const char* name);

// Target registration
void hub_register_target(Target* t);
void hub_unregister_target(TargetID id);
Target* hub_get_target(TargetID id);
Target** hub_get_targets_by_type(TargetType type);

// Request routing
void hub_send_request(RequestType type, TargetID target);
void hub_send_request_data(RequestType type, TargetID target, void* data);

// Target resolution
TargetID hub_resolve_symbolic(TargetID symbolic);

// Event bus
void hub_emit(EventType type, TargetID target, void* data);
void hub_subscribe(EventType type, void (*handler)(Event*), void* userdata);
void hub_unsubscribe(EventType type, void (*handler)(Event*));
void hub_subscribe_target(EventType type, TargetID target, void (*handler)(Event*), void* userdata);

// Query API
Component* hub_get_component(const char* name);
Component** hub_get_components_for_target_type(TargetType type);

#endif // HUB_H
```

---

## Usage Example

```c
// Initialization
hub_init();

// Register components (each component does this at init)
hub_register_component(&fullscreen_component);
hub_register_component(&floating_component);
hub_register_component(&focus_component);
hub_register_component(&urgency_component);

// When a client is created (by listener, on ManageNotify)
Client* c = client_create(window);
hub_register_target(&c->target);  // target adopts compatible components

// Keybinding sends fullscreen request
void keybinding_fullscreen(void) {
    hub_send_request(REQ_CLIENT_FULLSCREEN, TARGET_CURRENT_CLIENT);
    // Fire-and-forget
}

// Or with data
void keybinding_move_to_tag(int tag) {
    RequestDataTagMove d = { .tag = tag };
    hub_send_request_data(REQ_MONITOR_TAG_VIEW, TARGET_CURRENT_MONITOR, &d);
}

// Listen for fullscreen completion
void my_fullscreen_listener(Event* e) {
    if (e->type == EVT_FULLSCREEN_ENTERED) {
        // Client is now fullscreen
    }
}
hub_subscribe(EVT_FULLSCREEN_ENTERED, my_fullscreen_listener, NULL);

void keybinding_fullscreen_with_callback(void) {
    uint64_t corr_id = generate_correlation_id();
    hub_subscribe_target(EVT_REQUEST_SUCCESS, current_client(), on_fullscreen_done, NULL);
    hub_send_request_with_correlation(REQ_CLIENT_FULLSCREEN, current_client(), corr_id);
}
```

---

## Testing Strategy

1. **Unit tests for router**: Send requests, verify correct component called
2. **Unit tests for event bus**: Subscribe/emit, verify handlers called
3. **Unit tests for target resolution**: Symbolic → concrete resolution
4. **Integration tests**: Full request → component → SM → event flow

---

*See also:*
- `architecture/overview.md` — High-level architecture
- `architecture/component.md` — Component interface and structure
- `architecture/target.md` — Target design and adoption
