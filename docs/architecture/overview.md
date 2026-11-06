# Architecture Overview

*Document status: Authoritative*
*Last updated: 2026-11-06*

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
1. Component receives the request
2. Executor calls `sm_transition()` on the target's machine
3. On success: the row's action did the X work, the machine moved, one event was emitted
4. On refusal: the machine stays, a failure event is emitted, nothing was written

---

## Target Adoption Model

A target adopts the components that are registered when it is created. There is no adopted-components list on the target and no `target_adopt()` call: `client_create()` and `monitor_create()` call `hub_register_target()`, which calls `hub_adopt_components_for_target()`, which calls `on_adopt(target)` on each registered component whose `accepted_target_names` include the target's type.

```c
Client* client_create(xcb_window_t window) {
    Client* c = malloc(sizeof(Client));
    c->window = window;

    // Registration is what adopts the components: it runs their on_adopt
    // hooks against this target. There is no separate adopt loop here.
    hub_register_target(&c->target);

    return c;
}
```

The component's `on_adopt` is where its state machine is created on that target. The machine is stored by name: `client_set_sm()` and `monitor_set_sm()` write it, and `client_get_sm()` and `monitor_get_sm()` read it. A component registered after the target exists is not adopted by it and gets the targets created after its own registration. [hub.md](hub.md#component-adoption) has the timing and what a component can do about targets that already exist.

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
│   sm_transition() on the target's machine                           │
│   On success: the row's action did the X work                       │
│   On refusal: failure event, state unchanged, nothing written       │
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

## Adding a Component to a Running WM

The hub registers components, routes requests, and emits events. It has no hot-plug path: `hub_register_component()` adds a component to the registry and the request index, but it does not adopt existing targets, and there is no `hub_notify_compatible_targets()`. A component registered at runtime is adopted only by targets created after it.

`hub_unregister_component()` removes a component from the registry and the request index. It does not call `on_unadopt` on the targets that adopted it, so the state machines it created stay on those targets.

The built-in components register at startup in `wm-components.c`, before monitors and clients are created, which is what gives them every target. [hub.md](hub.md#component-adoption) has the sequence.

---

## Directory Structure

Where the code lives is one table, the "Where the code lives" table in
`.agents/skills/wm-bootstrap/SKILL.md`. It is not copied here: a second copy
disagrees with the tree eventually, and the copy an agent reads first is the
one that sends it to a path that no longer exists.

---

*See also:*
- `architecture/state-machine.md` — SM framework details
- `architecture/hub.md` — Hub implementation details
- `architecture/component.md` — Component interface
- `architecture/decisions.md` — ⭐ Full decision log with rationale
- `architecture/xcb-integration.md` — XCB event bridge
- `IMPLEMENTATION-ROADMAP.md` — Implementation order
