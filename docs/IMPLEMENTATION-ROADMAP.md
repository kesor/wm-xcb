# Implementation Roadmap

*Implementation order, not progress. Last updated: 2026-11-01.*

This file says what depends on what, and why the order below is the cheapest
one. It does not say how much is done: ask the commands.

```bash
bd list --type=epic        # the areas of work, with what is in each
bd ready                   # what is unblocked right now, in priority order
bd show <id>               # the evidence and the acceptance criteria
make test && make test-x   # what the model does, and what X sees
```

For what works today, see [README.md](README.md#current-state).

---

## Why this order and not another

A previous version of this page ordered the code: hub, then components, then
extensions, each with a completion mark as its files landed. That order was
right and the marks were wrong — the hub, the state machines, tiling, and the
components all counted as done while the window manager put nothing on the
screen, because mapping, key grabs, and input focus were in none of the phases.
The X11 output path is now its own epic (`wm-4f6`), and every phase after it in
this file waits on it.

The rule that follows: a phase is finished when a command says so, never when
its files exist.

---

## Phase 1: The model

Hub (registry, request router, event bus), the state machine framework, and the
targets (`Client`, `Monitor`, `Tag`) with their adoption path.

Everything after this registers against these, so a missing extension point
here costs an edit to a shared file in every later phase.

## Phase 2: The X11 output path

Connect, take substructure redirect, track windows as `Client` targets, map
them, place and tile them, grab keys, apply focus, and keep the event loop
blocked in `poll()` while idle. Epic `wm-4f6`.

This is where "it runs" becomes "it shows a window". Nothing user-visible works
before it, and every later phase assumes it: a component that changes state the
WM cannot display has no observable effect.

## Phase 3: The built-in components

Floating, urgency, pointer drag, then the bar. Epic `wm-rh6`.

Each one is a component on top of the phase 2 model, so it needs the model to
be observable before it can be seen working. The bar goes last because it
displays state that the other components produce.

## Phase 4: Usable without a compiler

Runtime configuration, reload without restart, and errors that say what is
wrong instead of failing silently. Epic `wm-lle`.

The compiled-in defaults in `src/config/config.def.h` and `src/config/config.wm.def.h` are a
development convenience; the promise in [PHILOSOPHY.md](PHILOSOPHY.md) is that a
user changes keybindings and rules in a text file. This phase is ordered after
the WM works because a configuration file for a WM that shows nothing is not
worth writing.

## Phase 5: Extensions

Each design in [extensions/](extensions/README.md) either becomes a component
with a test, or is rejected in
[architecture/decisions.md](architecture/decisions.md) with a reason. Epic
`wm-m9n`.

They come last for the reason the project exists: an extension that needs an
edit to a file it does not own is evidence that an earlier phase is missing an
extension point. Doing them after phases 1-4 makes that show up as a design
problem instead of an edit to shared code.

---

## Running in parallel

Phases 1 and 2 are not as separable as the list suggests: mapping a window,
assigning it a monitor and tags, tiling it, and focusing it all touch the same
component init and client-list code. Treat `wm-4f6` as one queue and check
`bd show` for blockers before starting a second issue in it; two agents editing
`client-list.c` and `src/core/wm.c` at once is how a merge becomes a redesign.

---

*See also: [plans/wm-architecture.md](../plans/wm-architecture.md) for the
per-phase design that the early phases were built from, and
[architecture/overview.md](architecture/overview.md) for how the pieces fit.*
