# Architecture Documentation

*Status: Authoritative*
*Last updated: 2026-04-30*

> **The Paradigm Shift:** State machines are the only authority on state mutations. Components never patch core code. The Hub connects everything. Read [VISION.md](../VISION.md) for the inspiration.

This directory contains the authoritative documentation for the wm architecture.

---

## Core Documents

| Document | Purpose |
|----------|---------|
| **[overview.md](overview.md)** | High-level architecture, core concepts, directory structure |
| **[hub.md](hub.md)** | Hub implementation: registry, router, event bus, target resolution |
| **[state-machine.md](state-machine.md)** | State machine framework: templates, transitions, guards, hooks |
| **[component.md](component.md)** | Component design: executors, listeners, SM templates, lifecycle |
| **[target.md](target.md)** | Target design: Client, Monitor, adoption, on-demand SM allocation |
| **[xcb-integration.md](xcb-integration.md)** | XCB event bridge: raw events → component handlers → Hub |
| **[decisions.md](decisions.md)** | ⭐ Decision log with rationale for all major choices |

---

## Quick Reference

### Core Concepts

- **Target**: Entity that owns state machines (Client, Monitor)
- **State Machine**: Tracks mutually exclusive state with transitions and events
- **Component**: Driver with executor (handles requests) and listener (handles events)
- **Hub**: Central orchestrator connecting components, targets, requests, and events

### Two Types of Writers

- **Raw Writer**: Authoritative state change (hardware events). No guards. Always succeeds.
- **Request Writer**: Intent to change state (keybindings, plugins). May fail guards.

### Event Flow

```
X Event → Hub → Component Listener → raw_write SM → SM emits event → Event Bus → Subscribers
```

### Request Flow

```
Keybinding → Hub (resolves target) → Component Executor → XCB → on success: raw_write SM → emit event
```

---

## Directory Structure

```
wm/
├── src/
│   ├── hub/           # Hub implementation
│   ├── sm/            # State machine framework
│   ├── target/        # Target infrastructure (Client, Monitor)
│   ├── components/    # Built-in components
│   └── listeners/     # Event listeners
└── docs/
    └── architecture/  # This directory
```

---

## Reading Order

For understanding the architecture:

1. **overview.md** — get the big picture
2. **decisions.md** — understand why decisions were made
3. **target.md** — understand entities (Client, Monitor)
4. **state-machine.md** — understand state tracking
5. **component.md** — understand extensibility
6. **hub.md** — understand central orchestration

For implementation:

1. Start with **hub.md** — implement the hub first
2. Then **state-machine.md** — implement the SM framework
3. Then **target.md** — implement Client and Monitor with adoption
4. Then **component.md** — implement built-in components
5. Wire up listeners

---

## Extension Documents

Extension designs are in `../extensions/`. Each extension doc describes:
- Overview and purpose
- State machine events
- Component hooks
- Configuration

See `../extensions/README.md` for the index.

---

## Reference: dwm Analysis

The dwm analysis in `../../dwm/docs/` documents the existing implementation that inspired this design. Key documents:

- `state-machines-architecture.md` — 12 state machines identified in dwm
- `globals.md` — Client and Monitor struct definitions
- Individual function docs for understanding dwm behavior

---

*End of architecture index*
