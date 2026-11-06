# Extension Registry

*Last updated: 2026-11-06*

This directory contains design documents for all planned extensions for the `./wm/` window manager. Each extension is designed as a **Component** that registers with the Hub. The audit below records which designs can be built that way today and which cannot.

## Standardized Format

Each extension document follows this structure:
- **Overview** — What it does
- **DWM Reference** — Corresponding dwm patch/function
- **State Machine Events** — Events subscribed to and emitted
- **Component Design** — Target type, SM template, Hub registration
- **Hooks** — Event subscription callbacks
- **Configuration** — Config struct and keybindings
- **Interactions with Other Components** — How it works with other extensions

## Extension Index

| ID | Name | Priority | DWM Equivalent | Dependencies |
|----|------|----------|----------------|--------------|
| [gap](gap.md) | Gap Plugin | High | dwm-gaps | Core tiling |
| [pertag](pertag.md) | Per-Tag Settings | High | dwmpertag | Core tags |
| [floatpos](floatpos.md) | Floating Position Rules | High | dwm-floatpos | Core floating |
| [urgency](urgency.md) | Urgency Notifications | Medium | dwm-urgencyhook | Core focus |
| [scratchpad](scratchpad.md) | Scratchpads | High | dwm-scratchpads | Core tags |
| [bstack](bstack.md) | BStack Layout | Medium | dwm-bstack | Core tiling |
| [cfact](cfact.md) | Client Factor (Relative Sizing) | Medium | dwm-cfacts | Core tiling |
| [actualfullscreen](actualfullscreen.md) | Actual Fullscreen | Medium | dwm-actualfullscreen | Core fullscreen |
| [resizecorners](resizecorners.md) | Resize Corners | Low | dwm-resizecorners | Core resize |
| [focus-adjacent](focus-adjacent.md) | Focus Adjacent Tag | Low | dwm-focusadjacenttag | Core tags |
| [ewmh-desktop](ewmh-desktop.md) | EWMH Desktop Support | High | dwm-ewmh | Core tags |
| [bar-style](bar-style.md) | Bar Padding/Styling | Medium | dwm-bar-padding | Core bar |
| [attach-policy](attach-policy.md) | Client Insertion Policy | Low | dwm-attach-aside-and-below | Core attach |

## Status and the extension point audit

*Audited 2026-11-06. This table is the only copy of the verdicts; the design
documents do not repeat them.*

Every design was run through the three questions in
[PHILOSOPHY.md section 4](../PHILOSOPHY.md#4-the-extension-point-test-in-practice).
A design is recorded under the strictest verdict it needs:

- **additive** — new files only: a component, its state machine, its requests and
  its subscriptions. Nothing that exists today is edited.
- **new state** — it needs a state machine on a Client, Monitor or Tag that no
  component owns yet. Targets are the documented home for state, so this is still
  additive; the component that owns the behaviour adopts the machine.
- **core edit** — it needs an existing component to do something it cannot be
  asked to do today. The last column names the missing extension point.

| Design | Verdict | What it needs | Missing extension point |
|--------|---------|---------------|-------------------------|
| [gap](gap.md) | core edit | an inset on every side of each tiled area, and the gap size per monitor | tiling computes its areas from the monitor rectangle and reads no inset, so the arrange pass has to learn about it. The work area (wm-46k.18) lets a component supply the rectangle; GapSM on the monitor is new state, and additive once the work area exists. |
| [pertag](pertag.md) | core edit | per-tag layout, mfact, nmaster, focused client, and bar state | `tiling_get_mfact()` and `tiling_get_nmaster()` return component defaults and discard the monitor, so a restored value has nowhere to land. Layout parameters on the monitor (wm-46k.20). `src/components/pertag.c` already keeps the per-tag arrays. |
| [floatpos](floatpos.md) | additive | a matching rule on manage and on float, then a geometry write | the rules are readable (`config_get_float_rules()`, `config_should_float()`), the floating request and the managed and floated events exist, and `client_configure()` writes the rectangle. A per-client rule override would add a Client machine. |
| [urgency](urgency.md) | core edit | urgency state on the client, a bar indicator, and focus of a visible urgent client | focus has no request at all, so auto-focus would edit the focus component (wm-46k.22). `Client.urgent` is a plain field today (wm-46k.17), and the indicator needs the bar component (wm-rh6.4). |
| [scratchpad](scratchpad.md) | core edit | a scratch key per client, a tag mask the tag manager applies, and focus when shown | no request sets a client's tag mask: `REQ_TAG_CLIENT_TOGGLE` flips one bit on the target client (wm-46k.21). Focus: wm-46k.22. |
| [bstack](bstack.md) | core edit | a second arrange algorithm the monitor can select | the layout machine has one value, `LAYOUT_STATE_TILE`, and the arrange pass is a fixed call in the tiling component: layout registry (wm-46k.19). |
| [cfact](cfact.md) | core edit | a per-client factor the arrange pass reads | the arrange pass is a fixed function in the tiling component, so cfact has to edit it: layout registry (wm-46k.19), plus a Client machine for the factor. |
| [actualfullscreen](actualfullscreen.md) | core edit | a fullscreen rectangle that differs from the layout area, and a decision about the bar | `tiling_place_fullscreen_clients()` gives every fullscreen client the monitor rectangle and asks no machine for it: work area (wm-46k.18). Depends on the bar (wm-rh6.4). |
| [resizecorners](resizecorners.md) | new state | a drag machine on the client: idle, or dragging from one of the eight corner and edge positions | button and motion events need a passive grab, which a component places itself; `client_configure()` and `REQ_CLIENT_FLOAT` already exist. No core edit. |
| [focus-adjacent](focus-adjacent.md) | additive | the current tag and a request to view the neighbour | reads `Monitor.tagset` and sends `REQ_TAG_VIEW`. The tagset field duplicates the tag-view machine (wm-46k.6). Moving to an arbitrary tag mask is not expressible, which the design avoids by scoping itself to single-tag views. |
| [ewmh-desktop](ewmh-desktop.md) | additive | nothing | already built as a component: it sends `REQ_TAG_VIEW` and subscribes to `EVT_TAG_CHANGED`, and edits no other component. |
| [bar-style](bar-style.md) | core edit | a bar window per monitor and a strip tiling leaves to it | no bar component exists (wm-rh6.4), and tiling arranges over the whole monitor rectangle: work area (wm-46k.18). |
| [attach-policy](attach-policy.md) | core edit | an insertion position decided before the client is attached | `client_list_add()` always inserts at the head, and it runs inside client creation before `EVT_CLIENT_MANAGED` is emitted, so a subscriber can only rewrite the client's neighbours: insertion policy (wm-46k.23). |

Totals: 3 additive, 1 new state, 9 core edit.

### The extension points to build

| Extension point | Designs it makes additive | Issue |
|-----------------|---------------------------|-------|
| A work area on the monitor: the rectangle tiling arranges in, and the full rectangle a fullscreen client covers | gap, bar-style, actualfullscreen | wm-46k.18 |
| A layout registry: a layout is a registered name, symbol and arrange function | bstack, cfact | wm-46k.19 |
| Layout parameters on the monitor: mfact, nmaster, and the selected layout | pertag | wm-46k.20 |
| `REQ_CLIENT_FOCUS`, handled by the focus component | urgency, scratchpad | wm-46k.22 |
| A tag-manager request carrying a client's tag mask | scratchpad | wm-46k.21 |
| A registered client-list insertion policy | attach-policy | wm-46k.23 |

With these extension points in place, every design here is additive or new state:
the count of designs that need a core edit goes to zero.

### Defects the audit found

Both are filed, neither is fixed here:

- [urgency.md](urgency.md) plays its notification sound with `system()`, which
  blocks the event loop (wm-m9n.7).
- The header comment on `layout_sm_template_create()` in
  `src/components/tiling.h` documents `LAYOUT_STATE_MONOCLE` and
  `LAYOUT_STATE_FLOATING`, and two layout events, that neither the state enum
  nor the event table has (wm-utz.25).

## Priority Levels

- **High**: Essential features, commonly requested, needed for basic workflow
- **Medium**: Important but not essential, popular enhancements
- **Low**: Convenience features, specific use cases

## Cross-Extension Dependencies

```
gap ──────┬────── pertag ─────── ewmh-desktop
          │                      │
          └──────────┬───────────┘
                     │
         ┌──────────┴──────────┐
         │                     │
         ▼                     ▼
       bstack               bar-style
         │                     │
         └──────────┬──────────┘
                    │
                    ▼
                 cfact
                    │
         ┌──────────┴──────────┐
         │                     │
         ▼                     ▼
    floatpos             resizecorners
         │
         └──────────┬──────────┐
                    │          │
                    ▼          ▼
              scratchpad    urgency
                    │
                    ▼
              actualfullscreen

attach-policy ──→ (standalone, minimal deps)
```

## Implementation Order Suggestion

### Phase 1: Core Infrastructure
1. **ewmh-desktop** — Required for external tools to work with the WM
2. **pertag** — Foundation for multi-tag workflows

### Phase 2: Basic Extensions
3. **gap** — Very common feature, demonstrates hook system
4. **bstack** — New layout, proves layout extensibility
5. **cfact** — Relative sizing, enhances tiling

### Phase 3: User Experience
6. **floatpos** — Floating window positioning
7. **scratchpad** — Quick access windows
8. **bar-style** — Visual customization

### Phase 4: Polish
9. **urgency** — Attention-getting
10. **actualfullscreen** — Fullscreen enhancement
11. **resizecorners** — Resize UX
12. **focus-adjacent** — Navigation convenience

### Phase 5: Advanced
13. **attach-policy** — Client ordering

## Event Coverage

Each extension uses specific events. Here's the coverage:

| Event | Extensions |
|-------|------------|
| `EVT_TAG_VIEW_CHANGED` | pertag, urgency, focus-adjacent |
| `EVT_TAG_CHANGED` | ewmh-desktop |
| `EVT_CLIENT_MANAGED` | floatpos, pertag, attach-policy |
| `EVT_CLIENT_FOCUS_GAIN/LOSE` | urgency, pertag |
| `EVT_CLIENT_FLOAT_ENABLED/DISABLED` | floatpos, resizecorners |
| `EVT_CLIENT_FULLSCREEN_ENTER/EXIT` | actualfullscreen |
| `EVT_LAYOUT_CHANGED` | pertag, bstack, gap |
| `EVT_TILING_PARAM_CHANGED` | gap, cfact |
| `EVT_TILING_RECALCULATE` | cfact |
| `EVT_BAR_SHOWN/HIDDEN` | bar-style, actualfullscreen |
| `EVT_BAR_DRAW` | bar-style |
| `EVT_RESIZE_START/UPDATE/END` | resizecorners |
| `EVT_SCRATCHPAD_*` | scratchpad |

---

*Last updated: 2026-11-06*
*Total extensions: 13*
