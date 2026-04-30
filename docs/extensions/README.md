# Extension Registry

*Last updated: 2026-04-30*

This directory contains design documents for all planned extensions for the `./wm/` window manager. Each extension is a **Component** that registers with the Hub.

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
| `EVT_TAG_VIEW_CHANGED` | pertag, ewmh-desktop, urgency, focus-adjacent |
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

*Last updated: 2026-04-30*
*Total extensions: 13*
