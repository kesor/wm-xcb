# Architecture Overview

*Document status: Authoritative*
*Last updated: 2026-04-30*

> **TL;DR:** Extensions coexist without conflict by subscribing to events instead of patching code. Read [VISION.md](../VISION.md) for the inspiration and [decisions.md](decisions.md) for the rationale.

---

## Core Concepts

| Concept | Description |
|---------|-------------|
| **Target** | Entity that owns state machines (Client, Monitor, Tag) |
| **State Machine** | Tracks mutually exclusive state; lives on a target |
| **Component** | Driver with executor (requests), listener (events), SM template |
| **Hub** | Central orchestrator: registry, router, event bus |

See the detailed docs for each:
- [target.md](target.md) — Target design
- [state-machine.md](state-machine.md) — SM framework
- [component.md](component.md) — Component design
- [hub.md](hub.md) — Hub implementation

---

## Key Principles

| Principle | Description |
|-----------|-------------|
| **SMs live on Targets** | State machines belong to targets (Client, Monitor), not components |
| **Reality is Authoritative** | XCB hardware events raw-write to SMs; guards are bypassed |
| **Components Provide Templates** | Components define SM templates; targets own and allocate instances |
| **Components Don't Call Each Other** | All communication goes through the Hub |
| **Requests Are Fire-and-Forget** | Send a request; optionally listen for events |
| **Events Flow One Way** | State machines emit; components subscribe |

---

## Two Types of Writers to State Machines

### Raw Writer
**Authority:** External reality has changed — SM MUST accept this

Used by listeners that detect hardware/X protocol changes:
- XRandR notifications (monitor connected/disconnected)
- Client property changes
- ConfigureNotify events

```c
// Listener (RandR) detected monitor disconnected
void randr_on_disconnect(xcb_randr_output_t output) {
    MonitorSM* sm = target_get_sm(monitor, COMPONENT_CONNECTION);
    sm_raw_write(sm, MON_STATE_DISCONNECTED);  // Forced, no negotiation
    // SM transitions and emits EVT_MONITOR_DISCONNECTED internally
}
```

**Rules:**
- No validation needed — reality is authoritative
- SM transitions immediately
- Event is emitted on transition

### Request Writer
**Authority:** Intent to change external reality — "please do this if you can"

Used by keybindings, components, and other requesters:
```c
// User presses keybinding for fullscreen
void keybinding_fullscreen(void) {
    hub_send_request(REQ_CLIENT_FULLSCREEN, TARGET_CURRENT_CLIENT);
    // Fire-and-forget; can listen to success/failure events
}
```

**Rules:**
1. Component receives request
2. Executor tries to fulfill the request (XCB call)
3. On success: raw write to SM, SM transitions, event emitted
4. On failure: SM stays in current state, failure event emitted

---

## Target Adoption Model

When a target (Client, Monitor) is created, it adopts compatible components from the registry.

```c
Client* client_create(xcb_window_t window) {
    Client* c = malloc(sizeof(Client));
    c->window = window;
    c->adopted_components = NULL;
    c->state_machines = NULL;
    
    // Hub matches target type to components that accept TARGET_TYPE_CLIENT
    Component** compatible = hub_get_components_for_target(TARGET_TYPE_CLIENT);
    
    for (int i = 0; compatible[i]; i++) {
        component_adopt(c, compatible[i]);
    }
    
    return c;
}
```

**Adoption does:**
1. Attaches component's listener (filtered for this target's ID)
2. Attaches component's SM template (not yet allocated)

**On-demand state machine allocation:**

```c
// Target decides when to create SM - not at target creation
StateMachine* sm = target_get_sm(client, COMPONENT_FULLSCREEN);
// First call to sm_create() allocates and initializes the SM
// SM allocated only when actually needed
```

---

## XCB Event Flow (Components Own Their Handlers)

**Important:** Raw XCB events go DIRECTLY to component handlers, NOT through hub. Components register XCB handlers at init.

```
┌─────────────────────────────────────────────────────────────────────┐
│                       XCB Event Loop                                │
│   - Receives raw X events from X server                             │
│   - Routes to registered component handlers                         │
└─────────────────────────────────────────────────────────────────────┘
         │
         ├──────────────────────────────────────────────────────┐
         │                                                      │
         ▼                                                      ▼
┌─────────────────────────┐  ┌──────────────────────┐  ┌─────────────────────┐
│  Keybinding Component   │  │   Focus Component    │  │ Monitor-Manager     │
│                         │  │                      │  │   Component         │
│ handle_key(KEY_PRESS)   │  │ handle_enter(ENTER)  │  │ handle_randr(RANDR) │
│         │               │  │         │            │  │         │           │
│         ▼               │  │         ▼            │  │         ▼           │
│ hub_send_request()      │  │ sm_raw_write()       │  │ sm_raw_write()      │
│ (to hub for routing)    │  │ (to client->FocusSM) │  │ (to mon->ConnSM)    │
└─────────────────────────┘  └──────────────────────┘  └─────────────────────┘
```

Components own their XCB handlers. Handlers live INSIDE the component, not in a central place.

| Component | XCB Events It Handles |
|-----------|---------------------|
| keybinding | KEY_PRESS, KEY_RELEASE |
| pointer | BUTTON_PRESS, BUTTON_RELEASE, MOTION_NOTIFY |
| focus | ENTER_NOTIFY, FOCUS_IN, FOCUS_OUT |
| monitor-manager | RANDR_NOTIFY |
| client-list | CREATE_NOTIFY, DESTROY_NOTIFY, MAP_REQUEST |
| fullscreen | PROPERTY_NOTIFY |
| bar | EXPOSE |

## SM Event Flow (Hub Event Bus)

State machine transition events flow through hub's event bus:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    State Machine                                    │
│   sm_raw_write() or sm_transition() causes state change             │
│   Emits EVT_* to HUB event bus                                      │
└─────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         HUB Event Bus                               │
│   hub_emit(EVT_FULLSCREEN_ENTERED, client_id, data)                 │
│   All subscribers notified                                          │
└─────────────────────────────────────────────────────────────────────┘
         │
         │ Subscribers
         ▼
┌─────────────────────────┐  ┌─────────────────────────┐
│   Bar Component         │  │   Tiling Component      │
│   subscribed:           │  │   subscribed:           │
│   EVT_FULLSCREEN_*      │  │   EVT_FULLSCREEN_*      │
│   Action: redraw        │  │   Action: re-tile       │
└─────────────────────────┘  └─────────────────────────┘
```

---

## Request Flow

```
Keybinding / Plugin
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                              HUB                                    │
│   hub_send_request(REQ_CLIENT_FULLSCREEN, TARGET_CURRENT_CLIENT)    │
│   Request includes: type, target (maybe symbolic)                   │
└─────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Router                                      │
│   Finds component that handles REQ_CLIENT_FULLSCREEN                │
│   Resolves TARGET_CURRENT_CLIENT → concrete client ID               │
│   Routes to component's executor                                    │
└─────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   Component Executor                                │
│   Executor receives (Request* req) with concrete target             │
│   Does XCB call (non-blocking)                                      │
│   On success: sm_raw_write(target->sm, NEW_STATE)                   │
│   On failure: emits EVT_REQUEST_FAILED                              │
└─────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    State Machine                                    │
│   Transitions, emits event (EVT_FULLSCREEN_ENTERED)                 │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Target Resolution

Targets can be specified as:
1. **Symbolic references** — `TARGET_CURRENT_CLIENT`, `TARGET_CURRENT_MONITOR`
2. **Concrete IDs** — actual window ID, output ID

**Resolution happens in the hub** before routing to components:

```c
Target hub_resolve(Target t) {
    switch (t) {
        case TARGET_CURRENT_CLIENT:
            return focus_component_get_current_client();
        case TARGET_CURRENT_MONITOR:
            return monitor_component_get_current_monitor();
        default:
            return t;  // already concrete
    }
}
```

Components receive concrete target IDs and don't need to know how resolution works.

---

## Hot-Plugging Components

Components can be added or removed at runtime:

```c
// Register a new component
hub_register_component(&urgency_component);

// Existing targets can adopt it
target_adopt(existing_client, &urgency_component);

// Or: notify all compatible targets to adopt
hub_notify_compatible_targets(COMPONENT_URGENCY);
```

---

## Directory Structure

```
wm/
├── src/
│   ├── hub/                    # Hub implementation
│   │   ├── hub.c/h             # Main hub logic
│   │   ├── registry.c/h        # Component/target registry
│   │   ├── router.c/h         # Request routing
│   │   └── event_bus.c/h      # Event pub/sub
│   │
│   ├── sm/                     # State machine framework
│   │   ├── sm.c/h             # SM core
│   │   ├── sm_transition.c/h  # Transition logic
│   │   └── sm_template.c/h    # Template storage
│   │
│   ├── target/                # Target infrastructure
│   │   ├── target.c/h         # Base target
│   │   ├── client.c/h         # Client target
│   │   ├── monitor.c/h        # Monitor target
│   │   └── target_adoption.c/h
│   │
│   ├── components/            # Built-in components
│   │   ├── fullscreen.c/h
│   │   ├── floating.c/h
│   │   ├── focus.c/h
│   │   ├── connection.c/h     # Monitor connection
│   │   ├── resolution.c/h     # Monitor resolution
│   │   └── ... (others)
│   │
│   ├── listeners/             # Event listeners
│   │   ├── xcb_listener.c/h   # XCB event dispatcher
│   │   ├── randr_listener.c/h # RandR events
│   │   └── ... (others)
│   │
│   └── main.c                 # Entry point
│
├── docs/
│   ├── architecture/
│   │   ├── overview.md        # This file
│   │   ├── hub.md            # Hub detailed design
│   │   ├── state-machine.md  # SM framework design
│   │   ├── component.md      # Component design
│   │   ├── target.md         # Target design
│   │   ├── event-flow.md     # Event/request flows
│   │   └── decisions.md      # Decision log with rationale
│   │
│   └── extensions/            # Extension designs
│
├── config.def.h               # Configuration
├── Makefile
└── config.mk
```

---

*See also:*
- `architecture/state-machine.md` — SM framework details
- `architecture/hub.md` — Hub implementation details
- `architecture/component.md` — Component interface
- `architecture/decisions.md` — ⭐ Full decision log with rationale
- `architecture/xcb-integration.md` — XCB event bridge
- `IMPLEMENTATION-ROADMAP.md` — Implementation order
