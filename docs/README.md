# wm/ Project Documentation

*Last updated: 2026-11-02*

---

## Start Here

| Document | Purpose |
|----------|---------|
| **[PHILOSOPHY.md](PHILOSOPHY.md)** | ⭐ **The rules** — user promises, the four commitments, the state machine contract |
| **[TESTING.md](TESTING.md)** | How to run unit tests, a nested Xephyr session, and performance measurements |
| **[QUALITY.md](QUALITY.md)** | The gates: what runs on commit, push, and CI, and how to add one |
| **[VISION.md](VISION.md)** | Why we built this, the paradigm shift |
| **[IMPLEMENTATION-ROADMAP.md](IMPLEMENTATION-ROADMAP.md)** | Implementation order and why it is this order (status lives in `bd list --type=epic`) |
| **[configuration.md](configuration.md)** | The config file: a bind line changes keys without a rebuild |

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
  `MapRequest` with a border and `WM_STATE=Normal`, adopting the windows that
  were already on the display when the WM started, through that same map path,
  so they are tiled and tracked too (an override-redirect window, or one its
  client left unmapped, is skipped), attaching every new client to the selected
  monitor with that monitor's visible tags, tiling that monitor's visible
  clients when a window is managed or unmanaged so that each client's border
  falls inside the area it was given - neighbours touch, and a client alone on
  the monitor fills it - and honouring a client's minimum size from
  `WM_NORMAL_HINTS` at the ICCCM's fixed offsets, and tracking
  which client the pointer is over: a managed window selects pointer enter and
  leave, and the focus component raw-writes `FocusSM` from those events and
  points the X input focus at the focused window with `xcb_set_input_focus`;
  a newly managed client takes focus, and the newest remaining client takes it
  back when the focused one is unmanaged or destroyed. The
  keys come from the user's config file when it names a binding this keyboard
  can use, and from `config.wm.def.h` otherwise, and are grabbed on the root
  window when the keybinding component starts, with the CapsLock and NumLock
  masks a press can carry, and grabbed again on `MappingNotify`, so a press
  reaches the component that owns it. `docs/configuration.md` has the file
  format; a line the parser cannot read is reported once, with its line
  number, and the compiled bindings stay in place when the file names none.
  The tag-manager and tiling components start before monitor discovery, so
  every monitor is adopted with a tag-view and a layout state machine, and
  `tag-manager.view`, `tag-manager.toggle`, and `tag-manager.client-move` are
  in the registry. A unit test runs the startup order and fails if a component
  that adopts a monitor registers after the monitor manager. The components
  `main` starts register the actions they own. A unit test inits
  every owner and fails if a name in `config.wm.def.h` is not registered after
  that. Once the last component is up, startup checks every wired binding
  against the action registry and logs one error line per binding whose action
  nobody registered, naming the chord and the action; a binding that resolves
  is not reported, and the window manager still starts. `main` exits non-zero
  when a module on its startup check does not come
  up, and a unit test runs that startup and asks each of those modules whether
  it is up. An unmap the WM causes itself, hiding the clients of a tag that is
  no longer shown, leaves the client managed, so showing the tag again maps it
  back even when a later tag action has already shown the window again by the
  time the unmap is read; an unmap the WM did not ask for is the client
  withdrawing: it is unmanaged, `WM_STATE` goes to `Withdrawn`, and
  `EVT_CLIENT_UNMANAGED` is emitted. The event loop blocks in `poll(2)` on the
  X connection, a signal wakeup pipe and the pipes modules register with it, so
  an idle WM costs no CPU, and SIGTERM ends it. A reply is read the same way: a
  client's size hints and `_NET_WM_STATE`, and the RandR output list and CRTC
  geometry, are requested without waiting and picked up from a poll callback
  when the server answers, so none of them stops the loop. `MOD+p` forks a shell that runs
  `dmenu_path | dmenu` and registers the read end of the pipeline's stdout as
  one of those sources: the chosen line is read when it arrives and the WM runs
  it, and a cancelled prompt emits a failure event instead of blocking. dmenu
  itself holds the keyboard grab while it is up, so the keys a user presses then
  belong to it; what stays free is the WM's loop, which still handles X events
  and the other registered pipes.
- **Does not work:** it sets no EWMH root properties. Visible tags are stored
  twice: a tag view change updates the tag-view state machine, and a new client
  still copies its tags from `Monitor.tagset`, which no runtime path writes, so
  every new client lands on tag 1.

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
