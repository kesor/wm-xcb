# wm/ Project Documentation

*Last updated: 2026-04-30*

---

## Start Here

| Document | Purpose |
|----------|---------|
| **[VISION.md](VISION.md)** | ⭐ **The Vision** — Why we built this, the paradigm shift |
| **[IMPLEMENTATION-ROADMAP.md](IMPLEMENTATION-ROADMAP.md)** | Quick reference for implementation order |

---

## Core Architecture

| Document | Purpose |
|----------|---------|
| **[architecture/README.md](architecture/README.md)** | Index to all architecture docs |
| **[architecture/decisions.md](architecture/decisions.md)** | ⭐ **Decision log** — Why we made each choice |
| **[architecture/overview.md](architecture/overview.md)** | High-level concepts and event flows |
| **[architecture/hub.md](architecture/hub.md)** | Hub: registry, router, event bus |
| **[architecture/component.md](architecture/component.md)** | Component design |
| **[architecture/state-machine.md](architecture/state-machine.md)** | State machine framework |
| **[architecture/target.md](architecture/target.md)** | Target design (Client, Monitor) |
| **[architecture/xcb-integration.md](architecture/xcb-integration.md)** | XCB bridge to event-driven model |

---

## Key Principles

1. **State machines live on targets** — Each Client has its own FullscreenSM, not a global one.
2. **Components never patch core** — They subscribe to events and send requests through the Hub.
3. **Reality is authoritative** — XCB events raw-write to state machines; keybindings send requests.

---

## Extension Designs

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

See [extensions/README.md](extensions/README.md) for the full index.

---

## Reference: dwm Analysis

| Document | Purpose |
|----------|---------|
| `../dwm/docs/state-machines-architecture.md` | 12 state machines identified in dwm |
| `../dwm/docs/globals.md` | Client and Monitor struct definitions |
| `../dwm/docs/manage.md` | Window lifecycle |
| `../dwm/docs/focus.md` | Focus management |
| `../dwm/docs/view.md` | Tag/view system |
| `../dwm/docs/tile.md` | Tiling algorithm |
| + 17 more function docs | Full dwm coverage |

---

## What's Implemented

See [IMPLEMENTATION-ROADMAP.md](IMPLEMENTATION-ROADMAP.md) for the phased implementation plan.

Current codebase has:
- ✅ XCB connection and event loop
- ✅ Basic state machine (IDLE, WINDOW_MOVE, WINDOW_RESIZE)
- ✅ Window list management
- ✅ Signal handling and logging

---

## What's Next

See [plans/wm-architecture.md](../plans/wm-architecture.md) for detailed implementation phases.

---

*For archived documents (old design sessions), see [archive/](archive/)*
