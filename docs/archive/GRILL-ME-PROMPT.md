# wm/ — Grill-Me Session Prompt

*Generated: 2026-04-28*
*Purpose: Align understanding and make key design decisions before implementation*

---

## Context: What We Have

### The Vision

A new window manager (`./wm/`) built from scratch in C using XCB, with:
- **State machine architecture** at its core (not ad-hoc if/else)
- **Event-driven design** — state changes emit events, plugins subscribe
- **Plugin/extension system** — add features without modifying core code
- **No patch conflicts** — extensions that don't know about each other can coexist
- Reference: The architecture is inspired by XState (JS) but for C

**The core problem we're solving:** In dwm, patches modify the same source code. Apply patch A then patch B and they conflict. We want extensions that "just work" by subscribing to events, not by patching code.

---

## What We Did Today

### 1. Analyzed dwm (`./dwm/`) — 23 documented functions

Documents in `./dwm/docs/`:
- `globals.md` — all global variables and Client/Monitor structs
- `focus.md`, `manage.md`, `view.md`, `tag.md`, `setmfact.md`, `setcfact.md`
- `tile.md`, `monocle.md`, `togglefloating.md`, `setfullscreen.md`
- `resizeclient.md`, `resizemouse.md`, `movemouse.md`
- `showhide.md`, `restack.md`, `attach.md`, `unmanage.md`
- `setlayout.md`, `clientmessage.md`, `configurerequest.md`
- `state-machines-architecture.md` — 12 state machines identified:
  1. MonitorManager — multi-monitor state
  2. ClientLifecycle — window creation/destruction
  3. ClientFocus — keyboard focus
  4. TagViewState — tag selection
  5. LayoutState — layout selection
  6. ClientFloatingState — tile vs float
  7. ClientFullscreenState — fullscreen mode
  8. TilingState — mfact, nmaster, cfact
  9. BarState — bar visibility
  10. ClientUrgencyState — urgency hints
  11. ScratchpadState — scratchpad visibility
  12. MouseDragState — mouse drag operations

### 2. Reviewed existing wm/ codebase

Files in `./wm/`:
- `wm-states.c/h` — Basic state machine with 3 states: IDLE, WINDOW_MOVE, WINDOW_RESIZE
  - Has `StateTransition` table (defined in `config.def.h`)
  - Has `Context` struct holding event data
  - `handle_state_event()` dispatches to state handlers
- `wm-window-list.c/h` — Linked list with sentinel pattern for window tracking
  - `window_insert()`, `window_remove()`, `window_find()`
  - `wnd_node_t` struct with: window, title, parent, x/y/w/h, border_width, stack_mode, mapped
- `wm-xcb.c` — XCB connection, `handle_xcb_events()` event loop
- `wm-xcb-events.c/h` — 19 event handlers (create, destroy, map, button, key, motion, etc.)
- `wm-clients.c/h` — `client_configure()`, `client_show()`, `client_hide()`
- `wm-xcb-ewmh.c/h` — EWMH setup
- `config.def.h` — State transition table
- `Makefile`, `Dockerfile` — Build setup

### 3. Designed 13 Extensions (in `./wm/docs/extensions/`)

Each extension doc contains:
- Overview, DWM reference, state machine events, hook points, configuration, keybindings, interactions

Extensions designed:
1. **gap** — Configurable gaps between tiled windows
2. **pertag** — Per-tag layouts, mfact, nmaster, focus
3. **floatpos** — Rule-based floating window positioning
4. **urgency** — Sound/flashes on urgent clients
5. **scratchpad** — Hidden windows with quick toggle
6. **bstack** — Bottom-stack tiling layout
7. **cfact** — Per-client relative sizing
8. **actualfullscreen** — Fullscreen with bar visible
9. **resizecorners** — Corner-drag resize
10. **focus-adjacent** — Tag wrap-around navigation
11. **ewmh-desktop** — EWMH atoms for external tools
12. **bar-style** — Bar sizing/position config
13. **attach-policy** — Client insertion policy

### 4. Created Handoff Document

`./wm/docs/PRE-WORK_CHECKLIST.md` — everything needed to continue:
- Architecture docs still needed
- Decisions to make
- Implementation order
- Quick wins

---

## Key Concepts We've Discussed

### Event-Driven Architecture

```
┌─────────────────────────────────────────────────┐
│                    Event Bus                     │
│   (plugins subscribe, state machines emit)      │
└─────────────────────────────────────────────────┘
           ▲                      │
           │                      │ emit events
           │                      ▼
┌─────────────────────────────────────────────────┐
│              State Machine (Core)                │
│  - Only thing that can mutate state             │
│  - Guards validate transitions                   │
│  - Actions execute mutations                      │
│  - Emits events (never calls plugins directly)  │
└─────────────────────────────────────────────────┘
           ▲
           │ dispatch commands
           │
┌─────────────────────────────────────────────────┐
│                 Input (keys, mouse)              │
│  KeyPress/Mouse → Command → State Machine        │
└─────────────────────────────────────────────────┘
```

### Plugin Pattern

Plugins:
- Subscribe to events via the event bus
- Emit commands to the state machine
- **Never modify the state machine**
- **Never modify other plugins**

Example: Gap plugin
```c
// Subscribes to EVT_LAYOUT_CHANGED
// Sets mon->gap_size = config.gap_size
// Does NOT modify tile() function
```

### State Machine Framework (Conceptual)

```c
typedef struct StateMachine StateMachine;

StateMachine* sm_create(const char* name, void* context);

// States and transitions
void sm_define_state(StateMachine* sm, State* state);
void sm_define_transition(StateMachine* sm, Transition t);

// Hooks for plugins
void sm_add_hook(StateMachine* sm, HookPhase phase, HookFn fn, void* userdata);

// Process a command
bool sm_dispatch(StateMachine* sm, const Command* cmd);
```

---

## What's Already in wm/

### Current State Machine (Minimal)

```c
enum State {
    STATE_IDLE,
    STATE_WINDOW_MOVE,
    STATE_WINDOW_RESIZE
};

struct StateTransition {
    enum WindowType window_target_type;
    enum EventType event_type;
    enum ModKey mod;
    xcb_button_t btn;
    xcb_keycode_t key;
    enum State prev_state;
    enum State next_state;
};

struct Context {
    enum State state;
    void (*state_funcs[NUM_STATES])(struct Context* ctx);
    // ... event data
};
```

### Current Window List

```c
typedef struct wnd_node_t {
    struct wnd_node_t* next;
    struct wnd_node_t* prev;
    char* title;
    xcb_window_t parent;
    xcb_window_t window;
    int16_t x, y;
    uint16_t width, height;
    uint16_t border_width;
    enum xcb_stack_mode_t stack_mode;
    uint8_t mapped;
} wnd_node_t;
```

---

## What We Need to Decide

### 1. Architecture Questions

- **One SM per domain or one global SM?**
  - Per-domain: Client SM, Focus SM, TagView SM, etc. (like dwm's 12 state machines)
  - One global: Single state machine handling all events
  - Concern: How do per-monitor SMs coordinate? What is `selmon`?

- **Event naming convention?**
  - `EVT_CLIENT_MANAGED` (caps, underscore)
  - `client.managed` (lowercase, dot)
  - `ClientEvent.Managed` ( CamelCase)

- **Event ownership — who emits what?**
  - State machines emit events after transitions
  - Event handlers emit events before calling SM
  - Both?

- **Event data — copy on emit or shared?**
  - Copy: safer, more allocations
  - Shared: faster, must ensure not freed while in use

### 2. Plugin System Questions

- **Static or dynamic loading?**
  - Static: compile-time plugin registry, simpler
  - Dynamic: dlopen .so files, can reload without restart

- **Hook placement — in SM or centralized?**
  - In SM: `sm->pre_guards[]`, `sm->post_actions[]`
  - Centralized: global hook registry

- **Configuration — how do plugins store config?**
  - Plugin struct has `void* data`
  - Config parsed at load time
  - Runtime config via commands?

### 3. Domain Model Questions

- **Tag representation?**
  - Bitmask (like dwm): `uint32_t tags` where bit N = tag N
  - Enum (safer): `enum Tag { TAG_1, TAG_2, ... }`
  - Bitmask allows multi-tag views, enum is clearer

- **Multi-tag views?**
  - dwm supports viewing multiple tags at once (via `toggleview`)
  - Should wm support this?

- **selmon — global or per-context?**
  - Global `selmon` (like dwm)
  - Passed through context everywhere
  - Multiple selected monitors?

### 4. Implementation Questions

- **Minimum viable wm?**
  - What must work before any extensions?
  - Window tracking, basic tiling, basic focus, basic tags?

- **XCB vs Xlib?**
  - Already using XCB (good)
  - Stick with XCB for all new code

---

## The Big Questions for the Grill

### About the Core Architecture

1. **Should we have one StateMachine per domain (Client, Focus, Tag, Layout) or one monolithic SM?**
   - Pro of per-domain: isolation, easier to reason about
   - Con of per-domain: coordination, shared state
   - Pro of monolithic: simpler, one place to look
   - Con of monolithic: grows unwieldy

2. **How do monitors fit into the SM architecture?**
   - Is there a MonitorStateMachine?
   - Do we have one SM per monitor?
   - Is `selmon` global or threaded through context?

3. **What is the minimal set of events needed to support all 13 extensions?**
   - Can we start with fewer and add later?
   - What's truly core vs extension-specific?

### About Extensions

4. **Which extensions should be "built-in" vs "loadable plugins"?**
   - ewmh-desktop might need to be built-in (required for panels)
   - pertag might be built-in (foundation for workflows)
   - gap could be a plugin

5. **Should extensions be able to modify state machine behavior (guards, transitions) or only react?**
   - Current design: extensions can only react, not modify
   - Is this too restrictive?
   - What if we need extensions to add transitions?

### About the Implementation

6. **What's the minimum wm we can ship that demonstrates the architecture?**
   - Just Client lifecycle + Events?
   - Add tag system?
   - Add basic tile layout?

7. **How do we handle the transition from "everything in one file" to "modular"?**
   - Current `wm-states.c` is ~200 lines
   - We want `sm/`, `events/`, `domain/`, `plugins/` directories
   - When do we refactor?

---

## Documents That Exist

```
wm/
├── docs/
│   ├── PRE-WORK_CHECKLIST.md    # Handoff context
│   └── extensions/
│       ├── README.md            # Extension index
│       ├── gap.md                # 13 extension docs
│       ├── pertag.md
│       ├── floatpos.md
│       ├── urgency.md
│       ├── scratchpad.md
│       ├── bstack.md
│       ├── cfact.md
│       ├── actualfullscreen.md
│       ├── resizecorners.md
│       ├── focus-adjacent.md
│       ├── ewmh-desktop.md
│       ├── bar-style.md
│       └── attach-policy.md
└── (source files)

dwm/
├── docs/
│   ├── README.md                # Function doc index
│   ├── globals.md               # Client/Monitor structs
│   ├── state-machines-architecture.md  # 12-SM design
│   ├── focus.md, manage.md, view.md, ...  # 23 function docs
│   └── (many more function docs)
└── (source files)
```

---

## What's Been Resolved ✅

The grill-me session on 2026-04-28 resolved all pending questions:

### Architecture

| Decision | Choice | Document |
|----------|--------|----------|
| SM granularity | Per-domain per-target | `architecture/decisions.md` |
| SM lives on | Target (not component) | `architecture/decisions.md` |
| SM allocation | On-demand | `architecture/decisions.md` |
| Two writer types | Raw + Request | `architecture/decisions.md` |
| Requests | Fire-and-forget async | `architecture/decisions.md` |
| Component ownership | SM templates, not SMs | `architecture/decisions.md` |
| Component filtering | accepted_targets[] | `architecture/decisions.md` |
| Target resolution | In hub | `architecture/decisions.md` |
| Target adoption | Eager at creation | `architecture/decisions.md` |
| Hot-plugging | Supported | `architecture/decisions.md` |

### Key Concepts Resolved

- **Target** = entity that owns SMs (Client, Monitor)
- **State Machine** = tracks mutually exclusive state, lives on target
- **Component** = driver (executor + listener + SM template)
- **Hub** = central orchestrator (registry + router + event bus)
- **Raw Writer** = authoritative state change (hardware events, no guards)
- **Request Writer** = intent to change state (keybindings, plugins, with guards)


---

*Full decision log: `architecture/decisions.md`*
*Full architecture docs: `docs/architecture/`*

---

*End of prompt*
*Ready for grill-me session*