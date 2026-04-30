# The Vision

*A window manager built for the next 20 years.*

---

## The Problem with Patches

In dwm, every feature is a patch. Apply patch A, then patch B, and they conflict. You spend hours resolving merge conflicts, only to have patch C break everything. The "extensible" window manager becomes a maintenance burden.

**We want something different.**

---

## The Vision

**A window manager where extensions coexist without conflict.**

Not through clever patch management, not through fork-and-patch, but through genuine architectural separation.

### Three Pillars

**1. State machines are the only authority on state mutations.**

Everything that changes state — fullscreen, floating, tags, layout, urgency — goes through a state machine with guards, actions, and events. No direct manipulation. No hidden state.

**2. Extensions never patch core code.**

They only:
- **Subscribe to events** — "tell me when X happens"
- **Send requests** — "please do Y"
- **Provide state machine templates** — "here's how to track Z"

**3. The Hub connects everything.**

Components don't call each other directly. They communicate exclusively through the Hub's request router and event bus. This is what makes hot-plugging possible.

---

## What This Enables

- **Add a feature without touching core code.** Write a component, register it with the Hub.
- **Two extensions that modify the same area work together.** They subscribe to events, they don't modify each other.
- **Replace any built-in behavior.** The tiling layout is a component. The focus system is a component. Swap them out.
- **Test components in isolation.** No X server required. Feed events in, verify events out.
- **Debug with clarity.** Every state change emits an event. Trace the event bus to understand behavior.

---

## The dwm Experience, Built Right

dwm is beloved because it works simply and directly. Keybindings respond instantly. The code is readable. The behavior is predictable.

We keep that spirit. But we build it on a foundation that doesn't fight extensibility.

**The same dwm workflow:**
```
MOD+1 → view tag 1
MOD+Enter → terminal
MOD+Shift+c → close
```

**But with real extensibility:**
```
MOD+d → toggle gap (gap component)
MOD+Shift+1 → move to tag 1 (pertag component)
MOD+u → urgency notification (urgency component)
```

Every feature is a component. Components don't know about each other. They just work.

---

## Principles

1. **Decouple everything.** Components communicate through the Hub, never directly.
2. **State machines own state.** Targets own state machines. Components provide templates.
3. **Events flow one way.** State machines emit. Components subscribe. Never the reverse.
4. **Requests are fire-and-forget.** Send a request. Optionally listen for the result. Don't block.
5. **Reality is authoritative.** Hardware events (X server) always win. State machines must reflect reality.

---

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────┐
│                           HUB                               │
│  ┌───────────┐  ┌───────────┐  ┌───────────────────────┐  │
│  │ Registry  │  │  Router   │  │     Event Bus         │  │
│  │           │  │           │  │                       │  │
│  │ Components│  │ Requests  │  │ Subscribe / Emit      │  │
│  │ Targets   │  │ → Comps   │  │ All state transitions  │  │
│  └───────────┘  └───────────┘  └───────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │                │                    ▲
         │                │                    │
         ▼                ▼                    │
┌─────────────────┐  ┌─────────────────────┐   │
│     XCB         │  │     Component       │───┘
│   Events        │  │   (executor +       │
│   (raw input)   │  │    listener)        │
└─────────────────┘  └─────────────────────┘
                            │
                            ▼
                   ┌─────────────────────┐
                   │       Target        │
                   │  (Client/Monitor)   │
                   │                     │
                   │  ┌────────────────┐ │
                   │  │ State Machine  │ │
                   │  │ (lives here)   │ │
                   │  └────────────────┘ │
                   └─────────────────────┘
```

---

## What Makes This Different

| Traditional | This Architecture |
|-------------|------------------|
| Patch core code | Write a component |
| Modify shared state | Subscribe to events |
| One big file | Modular, testable |
| Conflicts on merge | Hot-pluggable |
| Hard to debug | Event traces |
| Replace fork | Replace component |

---

## Status

The architecture is designed. The documentation is in `docs/architecture/`. See `architecture/decisions.md` for why we made each choice.

The implementation has begun. See `plans/wm-architecture.md` for the phased approach.

---

*This is not dwm with patches. This is dwm's simplicity, built on modern software-engineering principles.*
