# Implementation Roadmap

*Replaces: `PRE-WORK_CHECKLIST.md` (superseded by architecture decisions)*
*Last updated: 2026-04-30 (audit #81)*

---

## Overview

This roadmap summarizes the implementation phases for the window manager. For detailed architectural decisions, see [architecture/decisions.md](architecture/decisions.md).

**Project Status: ~75% Complete**

The core infrastructure, basic window management, tiling, and tags are implemented. Remaining work focuses on completing built-in components (floating, urgency, bar, pointer) and extensions.

---

## Phase 1: Core Infrastructure ✅ COMPLETE

| Step | Component | Description | Status |
|------|-----------|-------------|--------|
| 1.1 | Event Bus | Pub/sub system with subscription API | ✅ Merged |
| 1.2 | State Machine Framework | Templates, transitions, guards, hooks | ✅ Merged |
| 1.3 | Hub | Registry, Router, Event Bus integration | ✅ Merged |
| 1.4 | Target Infrastructure | Client and Monitor entities with adoption | ✅ Merged |

---

## Phase 2: Basic Functionality ✅ COMPLETE

| Step | Component | Description | Status |
|------|-----------|-------------|--------|
| 2.1 | XCB Handler Registry | Component-owned X event handlers | ✅ Merged |
| 2.2 | Client Lifecycle | Manage/unmanage windows | ✅ Merged |
| 2.3 | Basic Focus | Focus tracking with FocusSM | ✅ Merged |
| 2.4 | Basic Tags | Tag view system with TagViewSM | ✅ Merged |
| 2.5 | Basic Tile Layout | Master/stack tiling | ✅ Merged |

---

## Phase 3: Built-in Components 🔄 PARTIAL

| Step | Component | Description | Status |
|------|-----------|-------------|--------|
| 3.1 | Fullscreen Component | Fullscreen toggle with FullscreenSM | ✅ Merged |
| 3.2 | Floating Component | Floating toggle with FloatingSM | ❌ Not started |
| 3.3 | Urgency Component | Urgency hint handling | ❌ Not started |
| 3.4 | Bar Component | Status bar with updates | ❌ Not started |
| 3.5 | Pointer Drag | Mouse move/resize operations | ❌ Not started |

---

## Phase 4: Extensions 📋 DOCUMENTED

See [extensions/README.md](extensions/README.md) for the full extension list.

| Priority | Extensions | Status |
|----------|------------|--------|
| Implemented | `pertag` | ✅ Done |
| High | `ewmh-desktop`, `gap`, `floatpos`, `scratchpad` | ❌ Not started |
| Medium | `bstack`, `cfact`, `actualfullscreen`, `bar-style` | ❌ Not started |
| Low | `resizecorners`, `focus-adjacent`, `attach-policy` | ❌ Not started |

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
   - Keybinding component (keyboard input → hub requests)
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
   - Bar component (status bar with tag/layout/focus display)
   - Pointer component (mouse drag for move/resize)

❌ Extensions
   - EWMH desktop integration (for polybar/waybar)
   - Gap extension (gaps between tiled windows)
   - Floatpos (floating window position rules)
   - Scratchpad (quick terminal access)
   - Other extensions from docs/extensions/

❌ Configuration
   - Keybinding configuration file (#80)
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

## Recommended Next Steps

1. **Implement Bar component** (#19) - Visual feedback for tag/layout/focus changes
2. **Implement Floating component** (#21) - Core floating window functionality
3. **Implement Urgency component** (#22) - Urgent window indicators
4. **Implement Pointer drag** (#20) - Complete mouse interaction
5. **Design keybinding config** (#80) - User-configurable keybindings

---

*See also: [plans/wm-architecture.md](../plans/wm-architecture.md) for detailed phased plan*
