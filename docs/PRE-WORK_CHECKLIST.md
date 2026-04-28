# wm/ Implementation — Pre-Work Checklist

## Context: What We Have

### From dwm/ (Reference Implementation)
- **23 function documentation files** in `./dwm/docs/` analyzing dwm's behavior
- **State machine architecture analysis** in `dwm/docs/state-machines-architecture.md` (26KB, 12 state machines)
- Globals, focus, manage, view, tile, togglefloating, setmfact, setfullscreen, etc.

### Extension Designs for wm/
- **13 extension documents** in `./wm/docs/extensions/` covering:
  - gap, pertag, floatpos, urgency, scratchpad, bstack, cfact, actualfullscreen, resizecorners, focus-adjacent, ewmh-desktop, bar-style, attach-policy

### Existing wm/ Codebase
- Basic state machine (`wm-states.c/h`) with: STATE_IDLE, STATE_WINDOW_MOVE, STATE_WINDOW_RESIZE
- Window list with sentinel pattern (`wm-window-list.c/h`)
- XCB event handling (`wm-xcb.c`, `wm-xcb-events.c`)
- Client operations (`wm-clients.c/h`)
- EWMH setup (`wm-xcb-ewmh.c/h`)

---

## What We Still Need to Design/Decide

### 1. Core Architecture Documents (Missing)

We have extension docs but no architecture docs for the core. Need to write:

- [ ] **`./wm/docs/architecture/event-bus.md`** — Pub/sub system design
  - Event types (the 20+ events listed in extension docs)
  - Subscription API
  - Event emission/consumption flow
  
- [ ] **`./wm/docs/architecture/state-machine-framework.md`** — SM core design
  - StateMachine struct
  - Transition table format
  - Guard/action function signature
  - Context management
  
- [ ] **`./wm/docs/architecture/plugin-system.md`** — How extensions load
  - Plugin interface (init, hooks, data)
  - Static vs dynamic loading
  - Plugin registry

- [ ] **`./wm/docs/architecture/hook-system.md`** — Extension points
  - Hook phases (pre_guard, post_guard, pre_action, post_action)
  - How hooks are called from state machines
  - Built-in vs custom hooks

- [ ] **`./wm/docs/architecture/domain-model.md`** — Core entities
  - Client struct design
  - Monitor struct design
  - TagSet value object
  - Geometry value object

### 2. Extension-to-Event Mapping

Need to consolidate what events each extension needs:

| Event | Used By | Priority |
|-------|---------|----------|
| EVT_CLIENT_MANAGED | floatpos, pertag, attach-policy | Core |
| EVT_CLIENT_UNMANAGED | pertag | Core |
| EVT_CLIENT_SHOWN/HIDDEN | (implied by tag change) | Core |
| EVT_CLIENT_FOCUS_GAIN/LOSE | pertag, urgency | Core |
| EVT_CLIENT_FLOAT_ENABLED/DISABLED | floatpos | Core |
| EVT_CLIENT_FLOAT_MOVED | floatpos | Core |
| EVT_CLIENT_FULLSCREEN_ENTER/EXIT | actualfullscreen | Core |
| EVT_CLIENT_URGENCY_SET/CLEAR | urgency | Core |
| EVT_TAG_VIEW_CHANGED | pertag, ewmh-desktop, urgency, focus-adjacent, bar-style | Core |
| EVT_LAYOUT_CHANGED | pertag, bstack, gap | Core |
| EVT_TILING_PARAM_CHANGED | gap, cfact | Core |
| EVT_TILING_RECALCULATE | cfact | Core |
| EVT_BAR_SHOWN/HIDDEN | bar-style, actualfullscreen | Core |
| EVT_BAR_DRAW | bar-style | Core |
| EVT_RESIZE_START/UPDATE/END | resizecorners | Optional |
| EVT_SCRATCHPAD_SHOWN/HIDDEN/SPAWNED | scratchpad | Core |
| EVT_MONITOR_CONNECTED/DISCONNECTED | (not in ext docs yet) | Later |
| EVT_DRAG_START/END | (mouse drag) | Core |

### 3. Decisions to Make

- [ ] **Event enum naming**: `EVT_CLIENT_MANAGED` or `client.managed` or `Client.Managed`?
- [ ] **SM per-domain or single SM**: One SM per client? One global SM? Per-monitor SMs?
- [ ] **Plugin loading**: Static (compile-time) or dynamic (dlopen)?
- [ ] **Hook storage**: In SM struct or centralized hook registry?
- [ ] **Memory management**: Who owns event data? Copy on emit or shared?
- [ ] **Thread safety**: Single-threaded event loop is fine, but guard against misuse?

### 4. dwm Concepts to Port

From dwm docs, need to implement:

| dwm Concept | wm Equivalent | Status |
|-------------|--------------|--------|
| Client.isfloating | client.is_floating | TODO |
| Client.isfullscreen | client.is_fullscreen | TODO |
| Client.tags (bitmask) | client.tags (uint32) | TODO |
| Client.isurgent | client.is_urgent | TODO |
| Client.scratchkey | client.scratchkey | TODO |
| Client.cfact | client.cfact | TODO (cfact ext) |
| Monitor.tagset[2], seltags | mon.tagset, mon.seltags | TODO |
| Monitor.nmaster, mfact | mon.nmaster, mon.mfact | TODO |
| Monitor.lt[2] | mon.layouts[2] | TODO |
| Monitor.showbar | mon.showbar | TODO |
| Monitor.clients, sel, stack | mon.clients, mon.sel, mon.stack | TODO |
| Pertag (per-tag arrays) | pertag (pertag ext) | TODO (pertag ext) |
| TAGMASK, LENGTH(tags) | TAGMASK, NUM_TAGS | TODO |

### 5. Immediate Implementation Order (Suggested)

```
Phase 1: Core Infrastructure (Must come first)
├── 1. Event bus (no deps)
├── 2. Basic state machine framework (depends on events)
├── 3. Client entity (basic struct)
├── 4. Monitor entity (basic struct)
├── 5. Core events (CLIENT_MANAGED, TAG_VIEW_CHANGED, etc.)
│
Phase 2: Basic Functionality (Uses Phase 1)
├── 6. Window list integration with event emission
├── 7. Client lifecycle (manage → managed, unmanage → destroyed)
├── 8. Basic focus (focus/unfocus events)
├── 9. Basic tag system (view tags, tagset)
├── 10. Basic tile layout (no cfact yet)
│
Phase 3: Extensions (Use Phase 2)
├── 11. ewmh-desktop (requires TAG_VIEW_CHANGED event)
├── 12. pertag (requires tag system)
├── 13. gap (requires tile layout)
└── ... rest of extensions
```

### 6. Questions to Answer Tomorrow

- How do we want to handle the "tag as bitmask" vs "tag as enum" distinction?
- Should we support multi-tag views? (dwm supports via `toggleview`)
- How does `selmon` vs per-monitor SMs work?
- What's the minimal viable wm that can actually manage windows?

---

## Files to Read/Review Tomorrow

1. **Existing wm code** (fresh read):
   - `wm-states.c/h` — existing SM framework to extend
   - `wm-window-list.c/h` — window tracking to integrate
   - `wm-xcb-events.c` — event sources

2. **Architecture docs to create**:
   - Start with `event-bus.md` — it's the foundation
   - Then `state-machine-framework.md`
   - Then `domain-model.md` (Client, Monitor structs)

3. **Reference from dwm**:
   - `dwm/docs/globals.md` — for Client/Monitor field definitions
   - `dwm/docs/manage.md` — for window lifecycle understanding
   - `dwm/docs/state-machines-architecture.md` — for the 12-SM design

---

## Quick Wins (Small Things to Implement First)

If we want to make progress while designing continues:

1. **Add event enum to wm-states.h** — Define all events from extension docs
2. **Add basic event emission to wm-window-list.c** — Emit on insert/remove
3. **Add hook struct to wm-states.h** — Basic hook mechanism
4. **Refine Client struct** — Add is_floating, is_fullscreen, tags, etc.

---

## Log of What We Did Today

- Discussed state machine architecture for extensibility
- Analyzed dwm's 23 documented functions
- Created 12-state-machine design for dwm
- Read existing wm/ codebase (wm-states.c, wm-window-list.c, wm-xcb-events.c, etc.)
- Created 13 extension design documents in `./wm/docs/extensions/`
- Identified need for core architecture docs

---

*Document created: 2026-04-28*
*Purpose: Handoff context for next session*
*Total extension docs: 13*
*Total dwm function docs analyzed: 23*