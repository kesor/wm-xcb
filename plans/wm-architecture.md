# Plan: wm Architecture Implementation

> Source PRD: `.github/ISSUE_TEMPLATE/prd.yml`

## Architectural Decisions

Durable decisions that apply across all phases:

- **Hub scope**: Request routing + event bus only. Raw XCB events do NOT flow through hub.
- **Components**: Self-contained containers owning all domain logic (XCB handlers, executor, listener, SM template)
- **XCB event ownership**: Each component registers its own XCB event handlers at init. Handlers live inside the component.
- **State machine location**: SMs live on targets (Client, Monitor), not on components. Components provide SM templates.
- **Two writer types**: Raw writer (authoritative, no guard) vs request (with guard, may fail)
- **Target ID system**: Window IDs for clients, output IDs for monitors, symbolic constants for `CURRENT_*`

---

## Phase 1: Hub Foundation

**User stories**: 4 (add feature by writing component), 5 (independently testable)

### What to build

Core orchestrator providing request routing and event bus.

- `hub_init()` / `hub_shutdown()`
- `hub_register_component()` / `hub_unregister_component()`
- `hub_register_target()` / `hub_unregister_target()`
- `hub_get_component_by_name()` / `hub_get_component_by_request()`
- `hub_get_target_by_id()` / `hub_get_targets_by_type()`
- `hub_send_request(RequestType, TargetID, data)`
- `hub_emit(EventType, TargetID, data)`
- `hub_subscribe(EventType, handler)` / `hub_unsubscribe()`

The hub does NOT handle XCB events. It only handles:
1. Component and target registration
2. Request routing (finds component by request type)
3. Event pub/sub (components emit events, others subscribe)

### Acceptance criteria

- [ ] Components can register themselves at startup
- [ ] Targets can register themselves at creation
- [ ] Request `REQ_FOO` routes to component that handles it
- [ ] Subscriber receives event after emission
- [ ] Hub can be shutdown cleanly (no leaks)

---

## Phase 2: State Machine Framework

**User stories**: 4 (add feature by writing component), 5 (independently testable), 21 (log transitions)

### What to build

Framework for targets to own state machines.

**SM Template:**
- `SMTemplate` struct: name, states[], transitions[], guards[], actions[], events[]
- `sm_template_create()` / `sm_template_destroy()`

**SM Instance:**
- `StateMachine` struct: name, current_state, owner (Target*), template, data
- `sm_create(Target*, SMTemplate*)` — lazy allocation
- `sm_destroy(StateMachine*)`
- `sm_raw_write(StateMachine*, uint32_t new_state)` — authoritative, no guard
- `sm_transition(StateMachine*, uint32_t target_state)` — with guard, emits event
- `sm_get_state(StateMachine*)`
- `sm_get_available_transitions(StateMachine*)` — returns states reachable from current

**Hook system (for plugins):**
- `sm_add_hook(StateMachine*, HookPhase, HookFn, userdata)`
- Hook phases: `PRE_GUARD`, `POST_GUARD`, `PRE_ACTION`, `POST_ACTION`, `PRE_EMIT`, `POST_EMIT`

### Acceptance criteria

- [ ] Template defines states and transitions
- [ ] `sm_transition()` validates guard, executes action, updates state, emits event
- [ ] `sm_transition()` fails if guard rejects
- [ ] `sm_raw_write()` updates state without guard
- [ ] `sm_raw_write()` emits same event as transition
- [ ] Hooks execute in correct phase order
- [ ] Available transitions query returns valid next states

---

## Phase 3: XCB Infrastructure

**User stories**: 4 (add feature by writing component)

### What to build

Minimal XCB event dispatch infrastructure (NOT a component).

**Existing code to adapt:**
- `wm-xcb.c` — connection, event loop
- Events currently handled directly in `handle_xcb_events()`

**What to build:**
- Event loop that dispatches to registered handler functions
- Handler registration API: `xcb_register_handler(event_type, handler_fn)`
- Components register their XCB handlers at init
- Handlers are functions inside components, called by XCB infrastructure

**Key principle:** XCB events go DIRECTLY to components, NOT through hub. Hub only used for:
- Keybinding component sending requests (`hub_send_request()`)
- Components emitting SM transition events (`hub_emit()`)

### Component → XCB Event Mapping

| Component | XCB Events It Handles |
|-----------|----------------------|
| keybinding | KEY_PRESS, KEY_RELEASE |
| pointer | BUTTON_PRESS, BUTTON_RELEASE, MOTION_NOTIFY |
| focus | ENTER_NOTIFY, FOCUS_IN, FOCUS_OUT |
| monitor-manager | RANDR_NOTIFY |
| client-list | CREATE_NOTIFY, DESTROY_NOTIFY, MAP_REQUEST, UNMAP_NOTIFY |
| fullscreen | PROPERTY_NOTIFY |
| urgency | PROPERTY_NOTIFY |
| bar | EXPOSE |

### Acceptance criteria

- [ ] Components register XCB handlers at init
- [ ] Event type routes to correct component handler
- [ ] Handler receives event data with target window ID
- [ ] Keybinding handler can call `hub_send_request()`
- [ ] Focus/urgency handler can call `sm_raw_write()` directly

---

## Phase 4: Client Target + Client-List Component

**User stories**: 3 (minimal WM), 7 (tile windows), 17 (floating), 18 (fullscreen), 19 (urgent)

### What to build

Client entity + client-list component (handles window lifecycle).

**Client Target struct:**
```c
struct Client {
    Target base;           // id = window, type = CLIENT
    xcb_window_t window;
    char* title;
    char* class_name;
    int x, y;
    uint16_t width, height;
    uint16_t border_width;
    Monitor* monitor;
    uint32_t tags;        // bitmask
    bool managed;
    Client* next;
    Client* prev;
};
```

**Client-list Component:**
- Handles: CREATE_NOTIFY, DESTROY_NOTIFY, MAP_REQUEST, UNMAP_NOTIFY
- Manages client list (linked list with sentinel — extend existing code)
- On CREATE_NOTIFY/MAP_REQUEST: `client_create()`, register with hub, adopt components
- On DESTROY_NOTIFY/UNMAP_NOTIFY: `client_destroy()`, unregister from hub

**Adoption:**
- Client created → adopts compatible components (fullscreen, floating, focus, urgency, etc.)
- SMs allocated lazily on first access

### Acceptance criteria

- [ ] New window → Client created → added to list
- [ ] Window destroyed → Client removed → list cleaned
- [ ] Client registers with hub as TARGET_TYPE_CLIENT
- [ ] Client adopts components matching TARGET_TYPE_CLIENT
- [ ] Client has client_list reference for ordering

---

## Phase 5: Monitor Target + Monitor-Manager Component

**User stories**: 3 (minimal WM), 10 (multi-monitor)

### What to build

Monitor entity + monitor-manager component (handles display detection).

**Monitor Target struct:**
```c
struct Monitor {
    Target base;           // id = output, type = MONITOR
    xcb_randr_output_t output;
    xcb_randr_crtc_t crtc;
    int x, y;
    uint16_t width, height;
    uint32_t tagset;
    float mfact;
    int nmaster;
    Client* clients;
    Client* sel;
    Client* stack;
    Monitor* next;
};
```

**Monitor-manager Component:**
- Handles: RANDR_NOTIFY (output connected/disconnected, resolution changed)
- Manages monitor list
- Tracks selected monitor (selmon equivalent)
- On RANDR event: create/destroy Monitor targets, raw_write to ConnectionSM/ResolutionSM

**Adoption:**
- Monitor created → adopts compatible components (connection, resolution, tiling, bar, etc.)

### Acceptance criteria

- [ ] RandR output connected → Monitor created
- [ ] RandR output disconnected → Monitor destroyed
- [ ] Monitor resolution changed → ResolutionSM raw_write
- [ ] Multiple monitors tracked in list
- [ ] Selected monitor trackable (selmon)

---

## Phase 6: Keybinding Component

**User stories**: 6 (keybindings work), 9 (keyboard navigation)

### What to build

Component that handles keyboard input and sends requests to hub.

**keybinding Component:**
- Handles: KEY_PRESS, KEY_RELEASE (via XCB handler registration)
- Parses key bindings from config
- On matching key: `hub_send_request(REQ_*, TARGET_*)`
- Key bindings map (keycode + modifiers) → (request type + target)

**Example bindings:**
- `Mod+Enter` → `hub_send_request(REQ_CLIENT_FOCUS, TARGET_CURRENT_CLIENT)`
- `Mod+Shift+Enter` → focus next/prev client
- `Mod+1-9` → `hub_send_request(REQ_TAG_VIEW, TARGET_CURRENT_MONITOR)`

### Acceptance criteria

- [ ] KeyPress with Mod+Enter triggers REQ_CLIENT_FOCUS
- [ ] KeyPress with Mod+1 triggers REQ_TAG_VIEW for tag 1
- [ ] KeyPress with Mod+Shift+1 triggers REQ_TAG_TOGGLE for tag 1
- [ ] Unknown key does nothing (no crash)

---

## Phase 7: Pointer Component

**User stories**: 6 (keybindings work), 8 (focus follows mouse)

### What to build

Component that handles mouse input for drag operations and focus.

**pointer Component:**
- Handles: BUTTON_PRESS, BUTTON_RELEASE, MOTION_NOTIFY
- Drag state machine for move/resize:
  - IDLE → DRAGGING → IDLE (on button release)
  - RESIZING → IDLE (on button release)
- Mouse pointer position determines target client (ENTER_NOTIFY also updates)

**Focus integration:**
- `handle_enter()` updates focus SM via raw_write
- `hub_send_request(REQ_CLIENT_FOCUS, client_id)` on pointer enter

### Acceptance criteria

- [ ] Button press on client → enters DRAGGING or RESIZING state
- [ ] Mouse motion while dragging → moves window
- [ ] Button release → exits drag state, sends hub request if needed
- [ ] Pointer entering client window → client receives focus

---

## Phase 8: Focus Component

**User stories**: 8 (focus follows mouse), 9 (keyboard navigation)

### What to build

Component that tracks and manages client focus state.

**focus Component:**
- Owns FocusSM template: FOCUSED ↔ UNFOCUSED
- Handles: ENTER_NOTIFY, FOCUS_IN, FOCUS_OUT (from pointer component or X)
- `sm_raw_write(client->FocusSM, FOCUSED)` on focus change
- `hub_send_request(REQ_CLIENT_FOCUS, client_id)` for keyboard focus (Mod+Enter)

**Focus stack:**
- Ordered client list for focus cycling (focusprev/focusnext)
- `hub_send_request(REQ_CLIENT_FOCUS_NEXT)` and `_PREV`

### Acceptance criteria

- [ ] FocusSM transitions on client focus change
- [ ] EVT_CLIENT_FOCUSED emitted on focus gain
- [ ] EVT_CLIENT_UNFOCUSED emitted on focus loss
- [ ] Focus cycling (prev/next) works via keybindings
- [ ] Focus follows mouse on enter

---

## Phase 9: Tiling Engine + Tag Manager

**User stories**: 7 (auto tile), 11 (tags/workspaces)

### What to build

Two components for layout and tag management.

**tiling-engine Component:**
- Owns LayoutSM template: TILE ↔ MONOCLE ↔ BSTACK ↔ FLOATING
- Handles: REQ_LAYOUT_SET, REQ_LAYOUT_NEXT
- Tile algorithm: master + stack arrangement
- On layout change: restack clients, emit EVT_LAYOUT_CHANGED
- Integrates with monitor's geometry and mfact

**tag-manager Component:**
- Owns TagViewSM template: tracks visible tag bitmask
- Handles: REQ_TAG_VIEW (show specific tag), REQ_TAG_TOGGLE, REQ_TAG_SHIFT
- Tag keybindings: Mod+1-9, Mod+Shift+1-9, Mod+Tab
- On tag change: update monitor's tagset, restack clients, emit EVT_TAG_CHANGED

**Shared data:**
- Tag state per monitor (pertag optional for follow-up)
- Client → monitor → tag association

### Acceptance criteria

- [ ] Mod+Enter tiles current client in master area
- [ ] Mod+t sets tile layout
- [ ] Mod+m sets monocle layout
- [ ] Mod+1 shows tag 1
- [ ] Mod+Shift+1 moves client to tag 1
- [ ] Clients on hidden tags are not visible

---

## Phase 10: Bar Component

**User stories**: 12 (see state at a glance), 22 (status bar)

### What to build

Status bar displaying WM state.

**bar Component:**
- Handles: EXPOSE (redraw on damage)
- Subscribes to events: EVT_TAG_CHANGED, EVT_LAYOUT_CHANGED, EVT_CLIENT_FOCUSED
- On subscribe event: redraw bar

**Bar content:**
- Tags: [1][2][3]... with highlight on visible
- Layout indicator: [T] [M] [B] [F]
- Client count or selected client title
- System info: date/time (optional)

**Positioning:**
- Below monitor content, respects monitor geometry
- Configurable height

### Acceptance criteria

- [ ] Bar visible on each monitor
- [ ] Tag numbers shown, current tag highlighted
- [ ] Layout symbol shown
- [ ] Bar updates when tag/layout/focus changes
- [ ] Date/time displayed (if implemented)

---

## Phase 11: Fullscreen + Floating + Urgency Components

**User stories**: 17 (floating), 18 (fullscreen), 19 (urgent)

### What to build

Three client-focused components.

**fullscreen Component:**
- Owns FullscreenSM template: FULLSCREEN ↔ WINDOWED
- Handles: REQ_CLIENT_FULLSCREEN
- On fullscreen: configure window fullscreen, raw_write(FULLSCREEN)
- Listens to PROPERTY_NOTIFY for _NET_WM_STATE changes

**floating Component:**
- Owns FloatingSM template: FLOATING ↔ TILED
- Handles: REQ_CLIENT_FLOAT, REQ_CLIENT_TILE
- On float: save geometry, enable floating
- On tile: restore geometry, enable tiling
- Integrates with tiling-engine for repositioning

**urgency Component:**
- Owns UrgencySM template: URGENT ↔ CALM
- Handles: REQ_CLEAR_URGENT
- Listens to PROPERTY_NOTIFY for WM_HINTS (urgency bit)
- On urgency: mark client, maybe flash border or play sound
- On clear: raw_write(CALM)

### Acceptance criteria

- [ ] Mod+f toggles fullscreen, EVT_FULLSCREEN_* emitted
- [ ] Mod+Shift+space toggles floating, EVT_TILING_* emitted
- [ ] Urgent client shows indicator (border flash)
- [ ] Mod+Shift+c clears urgency, EVT_URGENCY_* emitted
- [ ] Fullscreen/floating state preserved correctly

---

## Phase 12: Optional Extensions (Follow-up)

Not in initial scope. See `./docs/extensions/` for specifications.

- gap (gaps between tiled windows)
- pertag (per-tag layouts, mfact, nmaster)
- bstack (bottom-stack tiling layout)
- actualfullscreen (fullscreen with bar visible)
- resizecorners (corner-drag resize)
- focus-adjacent (tag wrap-around navigation)
- ewmh-desktop (EWMH atoms for external tools)
- bar-style (bar sizing/position config)
- attach-policy (client insertion policy)

---

## Testing Decisions

### What Makes a Good Test

- Test through hub interface (requests → events), not internal state
- Test SM behavior in isolation with mocked owner target
- Tests should be deterministic, fast, no external dependencies

### Modules to Test by Phase

| Phase | Modules to Test |
|-------|----------------|
| 1 | Hub registry, request routing, event subscription |
| 2 | SM template, SM transition, guards, hooks |
| 3 | Handler registration, event dispatch routing |
| 4 | Client lifecycle, adoption, list management |
| 5 | Monitor lifecycle, RandR events |
| 6 | Key binding matching, request emission |
| 7 | Focus transitions, focus stack |
| 8 | Layout algorithms, tag view changes |
| 9 | Bar rendering, event subscription |
| 10 | Fullscreen toggle, floating state |
| 11 | Urgency detection and clearing |

### Test Patterns

- Unit tests with assertions (similar to existing `test-wm-window-list.c`)
- Mock hub/SM for isolation testing
- Integration tests: request → component → SM → event

---

## Out of Scope

- Dynamic plugin loading (dlopen) for v1 — compile-time plugins only
- Persistence/session management
- Non-X11 backends
- Complex animations
- Optional extensions (Phase 12)

---

*See also:*
- `docs/architecture/overview.md` — Core concepts
- `docs/architecture/hub.md` — Hub interface details
- `docs/architecture/state-machine.md` — SM framework details
- `docs/architecture/component.md` — Component interface details
- `docs/architecture/target.md` — Target design details
