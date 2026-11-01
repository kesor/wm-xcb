# wm/ Project Documentation

*Last updated: 2026-11-01*

---

## Start Here

| Document | Purpose |
|----------|---------|
| **[STATUS.md](STATUS.md)** | ⭐ **Where the project stands** — audited state, open defects, what to fix first |
| **[PHILOSOPHY.md](PHILOSOPHY.md)** | ⭐ **The rules** — user promises, the four commitments, the state machine contract |
| **[TESTING.md](TESTING.md)** | How to run unit tests, a nested Xephyr session, and performance measurements |
| **[QUALITY.md](QUALITY.md)** | The gates: what runs on commit, push, and CI, and how to add one |
| **[VISION.md](VISION.md)** | Why we built this, the paradigm shift |
| **[IMPLEMENTATION-ROADMAP.md](IMPLEMENTATION-ROADMAP.md)** | Planned implementation order (its completion percentages predate the 2026-11-01 audit) |

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

## Reference: dwm analysis (not in this repository)

The design work started from a study of dwm. Those notes live in a dwm fork's
`docs/` directory, not here. If you keep a dwm checkout next to this repository
(`../dwm/`), the useful ones are:

| Document | Purpose |
|----------|---------|
| `docs/state-machines-architecture.md` | 12 state machines identified in dwm |
| `docs/globals.md` | Client and Monitor struct definitions |
| `docs/manage.md` | Window lifecycle |
| `docs/focus.md` | Focus management |
| `docs/view.md` | Tag/view system |
| `docs/tile.md` | Tiling algorithm |
| + 17 more function docs | Full dwm coverage |

Nothing in this repository depends on them being present.

---

## What's Implemented

See [STATUS.md](STATUS.md). In short: hub, state machine framework, targets, and
eight components exist and pass 645 unit-test assertions, but the WM does not
map or tile windows yet, so it shows nothing on screen.

---

*For archived documents (old design sessions), see [archive/](archive/)*
