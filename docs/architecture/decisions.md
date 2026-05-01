# Decision Log

*Document status: Authoritative — records design decisions and rationale*
*Last updated: 2026-04-28*

---

## Purpose

This document records key design decisions made during the architecture design phase, along with the reasoning behind each choice. It serves as a reference for future implementers to understand why the system is designed as it is.

---

## State Machine Architecture

### Decision: SMs Live on Targets, Not Components

**Decision:** State machines are owned by targets (Client, Monitor), not by components. Components provide SM templates.

**Rationale:**
- A client has its own FullscreenSM. Component fullscreen doesn't "have" a fullscreen SM — it provides the template.
- This keeps the object/state boundary clean: targets exist, state machines track transitions on those targets.
- Makes it easy to query "what is client X's fullscreen state?" by looking at the client's SM.

**Alternatives considered:**
- SM owned by component: Would mean one component, many clients = many instances needed, or global state.
- Global SM registry: Would couple SM to a specific target, making target lookup necessary anyway.

**Trade-offs:**
- Pro: Clean object/state boundary
- Pro: Natural unit testing (test SM in isolation)
- Con: Targets need to manage their SMs (but this is handled by adoption model)

---

### Decision: On-Demand SM Allocation

**Decision:** State machines are allocated on-demand — targets call `sm_create()` when first needed, not at target creation time.

**Rationale:**
- Most targets won't use all SMs. A client might never go fullscreen.
- Reduces memory usage.
- Don't pay for what you don't use.

**Alternatives considered:**
- Eager allocation at target creation: Wastes memory for unused SMs.
- Configurable per-component: Adds complexity without clear benefit.

**Trade-offs:**
- Pro: Memory efficient
- Pro: Simple implementation
- Con: First use has small allocation cost (acceptable)

---

### Decision: Mutually Exclusive States Within One SM

**Decision:** Each SM tracks one domain of mutually exclusive states. A client can have multiple SMs for different domains (fullscreen, floating, urgency).

**Rationale:**
- Clear boundary between concerns: "is this client tiled or floating?" is one SM, "is it fullscreen?" is another.
- Independent transitions: Client can go fullscreen while tiled (rare but valid) or tiled while fullscreen (also valid).
- Matches real-world semantics: Window is TILING and URGENT simultaneously.

**Example from dwm:**
```c
// dwm conflates these in Client struct:
bool isfloating;    // not "floating state"
bool isfullscreen;  // not "fullscreen state"
// But also:
int tags;  // bitmask, which is technically multi-state
```

**Alternatives considered:**
- Single SM with compound state: `TILED+NOT_FULLSCREEN`, `FLOATING+FULLSCREEN`. Exponential explosion of states.
- SM per target per domain: Already our model, just clarifying.

---

## Writer Types

### Decision: Two Types of Writers (Raw vs Request)

**Decision:** State machines accept two kinds of input:
1. **Raw writes** — authoritative state changes (hardware, X events). No guards.
2. **Transitions** — requested state changes (keybindings, components). Guards apply.

**Rationale:**
- Reality is authoritative: If X says the monitor is disconnected, the SM MUST reflect that.
- User requests are not authoritative: Just because user presses a key doesn't mean fullscreen will succeed (window might not support it).
- Different error handling: Raw writes always succeed; transitions might fail guards.

**Alternatives considered:**
- Unified writer with flags: `sm_write(sm, state, FORCED)`. Confusing semantics.
- Separate SM types: `AuthoritativeSM` vs `RequestSM`. Unnecessary duplication.

---

### Decision: Raw Writes Always Emit Events

**Decision:** When a raw write causes a state change, the SM emits the transition event, same as a transition.

**Rationale:**
- Subscribers (other components, UI) don't care how the state changed — they just care that it did.
- Simplifies event handling: Always subscribe to state transitions, never need separate "raw state changed" events.

**Trade-offs:**
- Pro: Consistent event model
- Con: Might emit rapid-fire events if hardware oscillates (mitigated by debouncing in listeners if needed)

---

### Decision: Requests Are Fire-and-Forget by Default

**Decision:** When a keybinding sends a request, it doesn't wait for the result. Success/failure is communicated via events that the requester can optionally listen to.

**Rationale:**
- Matches XCB's async nature: Send request, handle reply later.
- Keeps keybindings simple: Just dispatch and done.
- Optional callbacks for cases that need them: Animation sequences, user feedback.

**Alternatives considered:**
- Blocking requests: Would block the event loop, unacceptable.
- Promises/futures: Adds complexity for marginal benefit. Can be added later if needed.

**Trade-offs:**
- Pro: Simple
- Pro: Non-blocking
- Pro: Matches XCB paradigm
- Con: Caller can't get immediate return value (but can listen to events)

---

## Components

### Decision: Components Declare Accepted Target Types

**Decision:** Each component declares an array of target types it accepts (e.g., `{CLIENT, MONITOR}`). Targets only adopt compatible components.

**Rationale:**
- Clean filtering: A keyboard target doesn't accidentally adopt client components.
- Self-documenting: Component says "I work on clients and monitors, not on keyboards."
- Extensible: New component with new target type works automatically.

**Alternatives considered:**
- Component registers for specific target IDs: Too granular, doesn't scale.
- Global component list per target: Requires target to know all component types.

---

### Decision: Components Do Not Own SMs

**Decision:** Components provide SM templates; targets own and create SM instances. Components handle requests and events but don't hold SM state.

**Rationale:**
- Decoupling: Component can be swapped without affecting targets.
- Multiple targets, one component: Fullscreen component handles all clients, each client has its own SM.
- Testing: Can test component executor without creating real targets.

**Example:**
```c
// WRONG (old model):
struct FullscreenComponent {
    StateMachine* sm;  // Which SM? Which client?
};

// RIGHT (new model):
struct FullscreenComponent {
    // Just the executor and listener
    void (*executor)(Request* req);
    void (*listener)(Target* t, Event* e);
    SMTemplate* (*get_template)(void);
};
```

---

## Implementation Anti-Patterns (Lessons Learned)

### Anti-Pattern: Hardcoding SMs in Targets

**What went wrong:** Early implementations tried to put state machine stubs directly in target structs (Client, Monitor), defeating the purpose of component-based SM templates.

**Correct approach:**
- Targets have a "list of adopted components"
- Components provide SM templates via `get_sm_template()`
- Targets call `target_get_sm(target, "fullscreen")` to lazily create SMs
- Targets should NOT hardcode references to specific SMs like `FocusSM`, `FullscreenSM`

**Wrong:**
```c
// WRONG - Client target hardcoding SMs
struct Client {
    // ...
    StateMachine* fullscreen_sm;  // ❌ Hardcoded!
    StateMachine* floating_sm;    // ❌ Hardcoded!
    StateMachine* focus_sm;       // ❌ Hardcoded!
    StateMachine* urgency_sm;     // ❌ Hardcoded!
};
```

**Correct:**
```c
// RIGHT - Generic SM lookup
struct Client {
    Target target;  // Inherits SM array from base Target
    // ...
};

// Get SM on-demand from adopted component:
StateMachine* sm = client_get_sm(c, "fullscreen");  // ✅ Generic lookup
```

### Anti-Pattern: Components Hardcoding Keybindings

**What went wrong:** The keybinding component hardcoded bindings for actions from other components (e.g., tag switching), violating the separation of concerns.

**Correct approach:**
- Keybinding component should only handle KEY_PRESS/KEY_RELEASE → dispatch
- Component actions should be exposed as callable functions
- A configuration mechanism should wire key combinations to component actions
- Components should NOT know about other components' keybindings

**Wrong:**
```c
// WRONG - Keybinding component knowing about tags
void keybinding_init(void) {
    // ❌ Hardcoded! TagManager should own this binding
    register_keybinding(XK_1, tag_view, 1);
    register_keybinding(XK_2, tag_view, 2);
    // ...
}
```

**Correct (RESOLVED - PR #80):**
```c
// keybindings.h - User-configurable keybindings
static const KeyBinding keybindings[] = {
    { MODKEY, 10, KEYBINDING_ACTION_TAG_VIEW, 1 },  // Mod+1
    { MODKEY, 11, KEYBINDING_ACTION_TAG_VIEW, 2 },  // Mod+2
    // ... add more as needed
    { 0, 0, 0, 0 }  // terminator
};
```

**Implementation:** Keybindings are now configurable via `src/components/keybindings.h`. Users modify this header and recompile.

---

## Pending Design: Actions and Wiring

### Problem

We don't yet have a defined mechanism for:
1. Components to expose callable actions (e.g., `fullscreen.toggle()`, `tag_manager.view(1)`)
2. Configuration to wire these actions to keybindings, pointer events, or other triggers
3. Cross-component wiring (e.g., pointer drag → resize action in another component)

### What We Need

- **Actions API**: Components expose a registry of named actions with signatures
- **Wiring mechanism**: How configuration binds triggers to actions
- **Target resolution**: How actions receive the correct target (current client, current monitor, etc.)

### Related Issues

- See GitHub issue: **#83 Design: Actions and Wiring Architecture**

---

## Pending Design: Configuration System

### Problem

Currently:
- Keybindings are hardcoded in the keybinding component
- No way for users to configure component properties (gaps, colors, rules)
- DWM-style `Rules` (auto-float certain windows) not implemented

### What We Need

1. **config.def.h style**: Compile-time configuration like dwm
2. **Runtime config**: File-based or UI-based configuration
3. **Configuration component**: Reads config and wires things up

### Scope

- Keybindings: `Mod+f` → `fullscreen.toggle(focused_client)`
- Rules: `class=Steam` → `floating.enable()`, `tags=1`
- Properties: `gaps=10`, `border_color=#333333`

### Related Issues

- See GitHub issue: **#84 Design: Configuration System for Keybindings, Rules, and Properties**

---

## Target System

### Decision: Targets Own Properties, Not State

**Decision:** Target objects (Client, Monitor) hold properties (geometry, title, resolution). State machines track state that transitions.

**Rationale:**
- Clear boundary: Properties change arbitrarily; state changes are controlled.
- Properties don't need guards; state does.
- Makes it easy to see what's "just data" vs "controlled state."

**Examples:**

| Properties (Target) | State (State Machine) |
|---------------------|---------------------|
| window ID, title, class | fullscreen status |
| x, y, width, height | tiling status |
| output ID, resolution | connection status |
| tagset, mfact, nmaster | layout selection |

**Note:** Some properties ARE controlled by state machines (e.g., monitor resolution → ResolutionSM). But the property holds the current value; the SM tracks the domain.

---

### Decision: Eager Component Adoption, On-Demand SM Allocation

**Decision:** When a target is created, it immediately adopts all compatible components (eager adoption). However, SMs are allocated on-demand (only when first needed).

**Rationale:**
- **Eager adoption:** Simplicity — don't need to track "which components have been adopted but SM not created." Built-in functionality is always available.
- **On-demand SM allocation:** Most targets won't use all SMs. Reduces memory usage.

**Alternatives considered:**
- Fully lazy adoption: Components adopted on first request. Adds tracking complexity.
- Config-driven adoption: User specifies what each target adopts. Too much config for v1.

---

### Decision: Symbolic Targets Resolved in Hub

**Decision:** Symbolic targets like `TARGET_CURRENT_CLIENT` are resolved to concrete IDs in the hub before routing requests.

**Rationale:**
- Components receive concrete IDs: Don't need to know how "current" is tracked.
- Centralized resolution: One place to change how targets are resolved.
- Testable: Can mock resolution in tests.

**Alternatives considered:**
- Resolution in component: Each component would need to know about symbolic targets.
- Symbolic targets passed through: Components would need to resolve, coupling them to resolution logic.

---

## Event System

### Decision: All Events Flow Through Event Bus

**Decision:** Components don't call each other directly. All communication is via:
1. Requests → Hub → Component
2. Events → Hub → Event Bus → Subscribers

**Rationale:**
- Decoupling: Components don't know each other's names.
- Extensible: New component subscribes to events, doesn't modify existing components.
- Debuggable: Easy to trace event flow.
- Hot-pluggable: Can add/remove components without affecting others.

**Alternatives considered:**
- Direct component calls: Tight coupling, hard to extend.
- Shared memory + polling: Inefficient for event-driven paradigm.

---

### Decision: Events Include Target ID

**Decision:** All events include the target ID they're about, so subscribers can filter.

**Rationale:**
- Component can subscribe to "all fullscreen events" OR "fullscreen events for client X."
- Bar component can update for specific monitors.
- Reduces need for fine-grained event types.

**Alternatives considered:**
- Separate event types per target: Exponential explosion of event types.
- Subscriber-side filtering: Would require more complex subscription API.

---

## Hot-Plugging

### Decision: Components Can Be Loaded/Unloaded at Runtime

**Decision:** The system supports dynamic component loading/unloading via hub_register_component() and hub_unregister_component().

**Rationale:**
- Plugin extensibility: Users can add extensions without recompiling WM.
- Development: Can reload components during development.
- Experimentation: Try different layouts, behaviors without restart.

**Alternatives considered:**
- Static only: Simpler, but limits extensibility.
- Full module system: Over-engineered for v1.

**Trade-offs:**
- Pro: Extensible
- Con: More complex hub implementation
- Con: Memory management complexity on unload

---

## What We Decided NOT to Do

### No Authorization System

**Explicit decision:** We do NOT implement authorization, permissions, or access control in state machines.

**Rationale:**
- Minimal system: This is a window manager, not a security boundary.
- Complexity: Authorization would add significant complexity to every transition.
- Trust model: If you can run the WM, you own the session.

**Trade-offs:**
- Pro: Simple
- Con: One misbehaving component can break everything (but this is true anyway)

---

### No Executor State Machines (for v1)

**Explicit decision:** We do NOT implement state machines for executors (beyond request handling).

**Rationale:**
- Simple operations are atomic: Send XCB request, get reply. No intermediate states.
- Complex animations are extension territory: If a component wants animation states, it can implement its own.
- Keeps core simple: Just request → X → result.

**Trade-offs:**
- Pro: Simple core
- Con: Animations require component work

---

### No Multi-Threading (for v1)

**Explicit decision:** v1 is single-threaded.

**Rationale:**
- XCB is async: Events come in one at a time anyway.
- Simplicity: No mutexes, no race conditions.
- Complexity: Multi-threading would add significant complexity to SMs.

**Future:** If multi-threaded in future, each SM would need its own lock, or use a thread-safe event bus.

---

*See also:*
- `architecture/overview.md` — High-level architecture
- `architecture/hub.md` — Hub implementation
- `architecture/component.md` — Component design
- `architecture/target.md` — Target design
- `architecture/state-machine.md` — SM framework
