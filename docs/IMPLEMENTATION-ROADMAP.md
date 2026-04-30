# Implementation Roadmap

*Replaces: `PRE-WORK_CHECKLIST.md` (superseded by architecture decisions)*
*Last updated: 2026-04-30 (audit #81, updated with lessons learned)*

---

## Overview

This roadmap summarizes the implementation phases for the window manager. For detailed architectural decisions, see [architecture/decisions.md](architecture/decisions.md).

**Project Status: ~75% Complete**

The core infrastructure, basic window management, tiling, and tags are implemented. Remaining work focuses on completing built-in components (floating, urgency, bar, pointer) and extensions.

---

## Phase 1: Core Infrastructure ✅ COMPLETE

| Step | Component | Description | Status | Issue |
|------|-----------|-------------|--------|-------|
| 1.1 | Event Bus | Pub/sub system with subscription API | ✅ | #3 |
| 1.2 | State Machine Framework | Templates, transitions, guards, hooks | ✅ | #5-8 |
| 1.3 | Hub | Registry, Router, Event Bus integration | ✅ | #2, #4 |
| 1.4 | Target Infrastructure | Client and Monitor entities with adoption | ✅ | #11, #15 |

---

## Phase 2: Basic Functionality ✅ COMPLETE

| Step | Component | Description | Status | Issue |
|------|-----------|-------------|--------|-------|
| 2.1 | XCB Handler Registry | Component-owned X event handlers | ✅ | #9 |
| 2.2 | Client Lifecycle | Manage/unmanage windows | ✅ | #12 |
| 2.3 | Basic Focus | Focus tracking with FocusSM | ✅ | #14 |
| 2.4 | Basic Tags | Tag view system with TagViewSM | ✅ | #18 |
| 2.5 | Basic Tile Layout | Master/stack tiling | ✅ | #17 |

---

## Phase 3: Built-in Components 🔄 PARTIAL

| Step | Component | Description | Status | Issue |
|------|-----------|-------------|--------|-------|
| 3.1 | Fullscreen Component | Fullscreen toggle with FullscreenSM | ✅ | #13 |
| 3.2 | Floating Component | Floating toggle with FloatingSM | ❌ | #21 |
| 3.3 | Urgency Component | Urgency hint handling | ❌ | #22 |
| 3.4 | Pointer Drag | Mouse move/resize operations | ❌ | #20 |
| 3.5 | Bar Component | Status bar with updates | ❌ (lowest) | #19 |

> **Priority note:** Bar component is lowest priority - it depends on all other state being in place first, as it displays the current state of the WM.

---

## Phase 4: Design Work Needed 🔍

These are design issues (#83, #84) that require brainstorming before implementation.

| Issue | Title | Description |
|-------|-------|-------------|
| #83 | Design: Actions and Wiring | Define how components expose callable actions for wiring to keybindings/pointer |
| #84 | Design: Configuration System | Define how users configure keybindings, rules, and component properties |

---

## Phase 5: Extensions 📋 DOCUMENTED

See [extensions/README.md](extensions/README.md) for the full extension list.

| Priority | Extensions | Status |
|----------|------------|--------|
| Implemented | `pertag` | ✅ Done |
| High | `ewmh-desktop`, `gap`, `floatpos`, `scratchpad` | ❌ Not started |
| Medium | `bstack`, `cfact`, `actualfullscreen`, `bar-style` | ❌ Not started |
| Low | `resizecorners`, `focus-adjacent`, `attach-policy` | ❌ Not started |

---

## Audit Tasks

The following items need verification/audit before proceeding:

| Issue | Task | Description |
|-------|------|-------------|
| #85 | Audit Monitor adoption | Verify Monitor target has proper component adoption mechanism |
| #86 | Audit Tag adoption | Verify Tag target has proper component adoption mechanism |
| #87 | Human integration testing | Get WM to testable state and manually verify components work |

---

## Quick Reference: What's Implemented

```
✅ Core Infrastructure
   - Hub (registry, router, event bus)
   - State machine framework (templates, transitions, guards, hooks)
   - Target system (Client, Monitor, Tag entities with adoption)

✅ Components
   - Client-list component (window lifecycle management)
   - Monitor-manager component (RandR display detection)
   - Keybinding component (handles KEY_PRESS/KEY_RELEASE)
   - Focus component (focus tracking with FocusSM)
   - Fullscreen component (fullscreen toggle with FullscreenSM)
   - Tag-manager component (tag view with TagViewSM)
   - Pertag component (per-tag layouts and mfact)
   - Tiling component (master/stack layout engine)

✅ XCB Integration
   - XCB handler registration
   - Event loop integration
   - EWMH support (partial)
```

---

## Quick Reference: What's Needed

```
❌ Built-in Components
   - Floating component (FLOATING ↔ TILED state machine)
   - Urgency component (urgent window hints)
   - Pointer component (mouse drag for move/resize)
   - Bar component (status bar with tag/layout/focus display)

❌ Design Work
   - Actions and wiring architecture (#83)
   - Configuration system (#84)

❌ Extensions
   - EWMH desktop integration (for polybar/waybar)
   - Gap extension (gaps between tiled windows)
   - Floatpos (floating window position rules)
   - Scratchpad (quick terminal access)
   - Other extensions from docs/extensions/

❌ Testing
   - Human integration testing (get WM running, verify components)
```

---

## Architecture Documents

For implementation, start here:

1. **[VISION.md](VISION.md)** — The inspiration and paradigm shift
2. **[architecture/decisions.md](architecture/decisions.md)** — Why we made each choice, lessons learned
3. **[architecture/overview.md](architecture/overview.md)** — High-level concepts
4. **[architecture/hub.md](architecture/hub.md)** — Hub implementation
5. **[architecture/component.md](architecture/component.md)** — Component design
6. **[architecture/state-machine.md](architecture/state-machine.md)** — SM framework
7. **[architecture/target.md](architecture/target.md)** — Target design and adoption
8. **[architecture/xcb-integration.md](architecture/xcb-integration.md)** — XCB bridge

---

## Recommended Next Steps

1. **Complete Phase 3** - Implement floating, urgency, pointer components
2. **Design work** - Address #83 (Actions) and #84 (Configuration) before adding more components
3. **Testing** - Get to human integration testing as soon as possible
4. **Extensions** - Only after core components are solid

---

*See also: [plans/wm-architecture.md](../plans/wm-architecture.md) for detailed phased plan*
