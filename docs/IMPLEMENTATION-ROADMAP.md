# Implementation Roadmap

*Replaces: `PRE-WORK_CHECKLIST.md` (superseded by architecture decisions)*
*Last updated: 2026-04-30*

---

## Overview

This roadmap summarizes the implementation phases for the window manager. For detailed architectural decisions, see [architecture/decisions.md](architecture/decisions.md).

---

## Phase 1: Core Infrastructure

| Step | Component | Description | Dependencies |
|------|-----------|-------------|--------------|
| 1.1 | Event Bus | Pub/sub system with subscription API | None |
| 1.2 | State Machine Framework | Templates, transitions, guards, hooks | Event Bus |
| 1.3 | Hub | Registry, Router, Event Bus integration | Event Bus, SM Framework |
| 1.4 | Target Infrastructure | Client and Monitor entities with adoption | Hub |

**Exit criteria:** Basic Hub with registration, request routing, and event emission working.

---

## Phase 2: Basic Functionality

| Step | Component | Description | Dependencies |
|------|-----------|-------------|--------------|
| 2.1 | XCB Handler Registry | Component-owned X event handlers | None |
| 2.2 | Client Lifecycle | Manage/unmanage windows | Target Infrastructure |
| 2.3 | Basic Focus | Focus tracking with FocusSM | Client Lifecycle |
| 2.4 | Basic Tags | Tag view system with TagViewSM | Focus |
| 2.5 | Basic Tile Layout | Master/stack tiling | Tags |

**Exit criteria:** A working tiled window manager with tag navigation.

---

## Phase 3: Built-in Components

| Step | Component | Description | Dependencies |
|------|-----------|-------------|--------------|
| 3.1 | Fullscreen Component | Fullscreen toggle with FullscreenSM | Basic Tile |
| 3.2 | Floating Component | Floating toggle with FloatingSM | Basic Tile |
| 3.3 | Urgency Component | Urgency hint handling | Focus |
| 3.4 | Bar Component | Status bar with updates | Tags |

**Exit criteria:** All dwm built-in features work.

---

## Phase 4: Extensions

See [extensions/README.md](extensions/README.md) for the full extension list.

| Priority | Extensions |
|----------|------------|
| High | `ewmh-desktop`, `pertag`, `gap`, `floatpos`, `scratchpad` |
| Medium | `bstack`, `cfact`, `actualfullscreen`, `bar-style` |
| Low | `resizecorners`, `focus-adjacent`, `attach-policy` |

---

## Quick Reference: What's Implemented

From the existing codebase:

```
✅ XCB connection setup
✅ Event loop (handle_xcb_events)
✅ Basic state machine (IDLE, WINDOW_MOVE, WINDOW_RESIZE)
✅ Window list (insert, remove, find)
✅ Signal handling
✅ Logging
```

---

## Quick Reference: What's Needed

```
❌ Client entity (is_floating, tags, etc.)
❌ Monitor entity (tagset, mfact, layouts, etc.)
❌ Event bus (pub/sub)
❌ State machine framework
❌ Component system
❌ Basic tiling
❌ Basic focus
```

---

## Architecture Documents

For implementation, start here:

1. **[VISION.md](VISION.md)** — The inspiration and paradigm shift
2. **[architecture/decisions.md](architecture/decisions.md)** — Why we made each choice
3. **[architecture/overview.md](architecture/overview.md)** — High-level concepts
4. **[architecture/hub.md](architecture/hub.md)** — Hub implementation
5. **[architecture/component.md](architecture/component.md)** — Component design
6. **[architecture/state-machine.md](architecture/state-machine.md)** — SM framework
7. **[architecture/target.md](architecture/target.md)** — Target design
8. **[architecture/xcb-integration.md](architecture/xcb-integration.md)** — XCB bridge

---

*See also: [plans/wm-architecture.md](../plans/wm-architecture.md) for detailed phased plan*
