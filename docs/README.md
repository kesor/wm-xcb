# wm/ Project Documentation

*Last updated: 2026-11-01*

---

## Start Here

| Document | Purpose |
|----------|---------|
| **[PHILOSOPHY.md](PHILOSOPHY.md)** | ⭐ **The rules** — user promises, the four commitments, the state machine contract |
| **[TESTING.md](TESTING.md)** | How to run unit tests, a nested Xephyr session, and performance measurements |
| **[QUALITY.md](QUALITY.md)** | The gates: what runs on commit, push, and CI, and how to add one |
| **[VISION.md](VISION.md)** | Why we built this, the paradigm shift |
| **[IMPLEMENTATION-ROADMAP.md](IMPLEMENTATION-ROADMAP.md)** | Implementation order and why it is this order (status lives in `bd list --type=epic`) |

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

## Current state

Short version, and the parts that stay true until they are fixed:

- **Built:** hub (registry, request router, event bus), state machine framework,
  targets (`Client`, `Monitor`, `Tag`), and the built-in components. The unit
  suite passes.
- **Works against X:** connecting, taking substructure redirect, RandR monitor
  discovery, tracking windows as `Client` targets, mapping a window on
  `MapRequest` with a border and `WM_STATE=Normal`, attaching every new client
  to the selected monitor with that monitor's visible tags, and tiling that
  monitor's visible clients when a window is managed or unmanaged. Every action
  name `config.wm.def.h` wires resolves to a registered action, and a unit test
  fails when one stops resolving. No key is grabbed yet, so no keybinding can
  reach any of them from X.
- **Does not work:** it never grabs keys, so no binding fires; the focus and
  tag-manager components are never initialized (`bd show wm-4f6.14`,
  `bd show wm-4f6.13`), so mouse enter and leave reach no state machine and a
  tag action has no monitor tag view behind it; it never calls
  `xcb_set_input_focus`, so focus stays wherever the client left it; it sets no
  EWMH root properties. The event loop polls instead of blocking, so it burns a
  core while idle.

Do not take that from a document. Ask the tree:

```bash
make test && make test-x      # model, then what X sees
make check-async              # blocking calls in the event loop
bd ready                      # what is known and unblocked
bd list --type=epic           # the work in progress, grouped
```

Every defect behind the list above is a beads issue with the evidence in its
description: `bd show` the epic you are working near. Beads is the living
record; this file only says where to look.

---

*For archived documents (old design sessions), see [archive/](archive/)*
