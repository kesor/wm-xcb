# wm/ Project Overview

*Last updated: 2026-04-28*

---

## What Is This?

A new X11 window manager built in C with XCB, designed from scratch with:
- **State machine architecture** as the core
- **Event-driven plugin system** for extensibility
- **No patch conflicts** — extensions subscribe to events, don't modify code

Reference: dwm (`./dwm/`) — analyzed extensively for design patterns

---

## Documentation Index

### For Implementation (Start Here)

| Document | Purpose |
|----------|---------|
| **[GRILL-ME-PROMPT.md](GRILL-ME-PROMPT.md)** | ⭐ **START HERE TOMORROW** — Design decisions to align on |
| **[PRE-WORK_CHECKLIST.md](PRE-WORK_CHECKLIST.md)** | Handoff context, what was discussed today |

### Architecture (Core Framework)

| Document | Status |
|----------|--------|
| **[architecture/README.md](../docs/architecture/README.md)** | ⭐ START HERE — Index to architecture docs |
| **[architecture/overview.md](../docs/architecture/overview.md)** | ⭐ Core concepts, event/request flows, decisions |
| **[architecture/hub.md](../docs/architecture/hub.md)** | Hub: registry, router, event bus, target resolution |
| **[architecture/state-machine.md](../docs/architecture/state-machine.md)** | SM framework: templates, transitions, guards |
| **[architecture/component.md](../docs/architecture/component.md)** | Component: executor, listener, SM templates |
| **[architecture/target.md](../docs/architecture/target.md)** | Target: Client, Monitor, adoption model |
| **[architecture/decisions.md](../docs/architecture/decisions.md)** | Decision log with rationale |

### Extension Designs (Reference for Implementation)

In `extensions/`:

| Extension | Priority | DWM Equivalent |
|-----------|----------|----------------|
| [ewmh-desktop.md](extensions/ewmh-desktop.md) | High | dwm-ewmh |
| [pertag.md](extensions/pertag.md) | High | dwmpertag |
| [gap.md](extensions/gap.md) | High | dwm-gaps |
| [scratchpad.md](extensions/scratchpad.md) | High | dwm-scratchpads |
| [floatpos.md](extensions/floatpos.md) | High | dwm-floatpos |
| [bstack.md](extensions/bstack.md) | Medium | dwm-bstack |
| [cfact.md](extensions/cfact.md) | Medium | dwm-cfacts |
| [urgency.md](extensions/urgency.md) | Medium | dwm-urgencyhook |
| [actualfullscreen.md](extensions/actualfullscreen.md) | Medium | dwm-actualfullscreen |
| [bar-style.md](extensions/bar-style.md) | Medium | dwm-bar-padding |
| [resizecorners.md](extensions/resizecorners.md) | Low | dwm-resizecorners |
| [focus-adjacent.md](extensions/focus-adjacent.md) | Low | dwm-focusadjacenttag |
| [attach-policy.md](extensions/attach-policy.md) | Low | dwm-attach-aside-and-below |
| [README.md](extensions/README.md) | Index | — |

### Reference: dwm Analysis

| Document | Purpose |
|----------|---------|
| `dwm/docs/state-machines-architecture.md` | 12-state-machine design |
| `dwm/docs/globals.md` | Client/Monitor struct definitions |
| `dwm/docs/manage.md` | Window lifecycle |
| `dwm/docs/focus.md` | Focus management |
| `dwm/docs/view.md` | Tag/view system |
| `dwm/docs/tile.md` | Tiling algorithm |
| `dwm/docs/setmfact.md` | Master factor |
| `dwm/docs/setcfact.md` | Client factor |
| `dwm/docs/togglefloating.md` | Floating state |
| `dwm/docs/setfullscreen.md` | Fullscreen |
| `dwm/docs/resizemouse.md` | Resize drag |
| `dwm/docs/movemouse.md` | Move drag |
| + 13 more function docs | Full dwm coverage |

---

## Current Codebase

### Existing Files (./wm/)

```
wm/
├── wm.c                          # Main entry, event loop
├── wm.h                          # Empty (header stub)
├── wm-states.c/h                 # Basic state machine (3 states)
├── wm-window-list.c/h            # Window list (sentinel pattern)
├── wm-clients.c/h                # Window operations
├── wm-xcb.c/h                    # XCB connection
├── wm-xcb-events.c/h             # 19 event handlers
├── wm-xcb-ewmh.c/h               # EWMH setup
├── wm-kbd-mouse.c                # (stub)
├── wm-log.c/h                    # Logging
├── wm-signals.c/h                # Signal handling
├── wm-running.c/h               # running flag
├── config.def.h                 # State transitions
├── Makefile, Dockerfile
└── (vendor/, .git/, etc.)
```

### What's Implemented

- ✅ XCB connection setup
- ✅ Event loop (`handle_xcb_events()`)
- ✅ Basic state machine (IDLE, WINDOW_MOVE, WINDOW_RESIZE)
- ✅ Window list (insert, remove, find)
- ✅ Signal handling (SIGINT, SIGTERM, SIGCHLD)
- ✅ Logging system
- ✅ EWMH setup stub

### What's Missing (for basic wm)

- ❌ Client entity (with is_floating, tags, etc.)
- ❌ Monitor entity (with tagset, mfact, layouts, etc.)
- ❌ Event bus (pub/sub system)
- ❌ State machine framework (for extensions)
- ❌ Basic tiling layout
- ❌ Basic focus management
- ❌ Basic tag system
- ❌ Plugin system

---

## Key Design Decisions (Pending)

From `GRILL-ME-PROMPT.md`:

1. **State machine granularity**: One per domain or one global?
2. **Event naming convention**: `EVT_*` or `domain.event`?
3. **Plugin loading**: Static or dynamic?
4. **Hook placement**: In SM or centralized?
5. **selmon design**: Global or threaded through context?
6. **Minimum viable wm**: What works before extensions?

See [GRILL-ME-PROMPT.md](GRILL-ME-PROMPT.md) for the full list of questions.

---

## Implementation Order (Suggested)

```
Phase 1: Core Infrastructure
├── 1. Event bus
├── 2. State machine framework
├── 3. Client entity
├── 4. Monitor entity
└── 5. Core events

Phase 2: Basic Functionality
├── 6. Window list → event emission
├── 7. Client lifecycle (manage/unmanage)
├── 8. Basic focus
├── 9. Basic tags
└── 10. Basic tile layout

Phase 3: Extensions
├── ewmh-desktop
├── pertag
├── gap
├── floatpos
├── scratchpad
└── ... (rest of 13)
```

---

## Quick Stats

| Metric | Value |
|--------|-------|
| Extension docs written | 13 |
| dwm functions documented | 23 |
| State machines designed | 12 |
| Lines of documentation | ~3,500 |
| Lines of existing wm code | ~1,500 |

---

## How to Use This Repository

### To understand the design:
1. Read `GRILL-ME-PROMPT.md` — get context
2. Read `extensions/README.md` — see all extension ideas
3. Read individual extension docs for details

### To start implementing:
1. Run `/skill:grill-me` on `GRILL-ME-PROMPT.md`
2. Align on design decisions
3. Create `architecture/` docs based on decisions
4. Start with Phase 1: Event bus

---

*End of overview*