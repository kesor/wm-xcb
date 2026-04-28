# Extension: Pertag (Per-Tag Settings)

## Overview

Saves and restores per-tag settings including: layout, mfact, nmaster, bar visibility, and focused client. This allows each tag to have its own independent configuration. Switching tags restores all settings; switching back restores them again.

## DWM Reference

Based on `view.md`, `setmfact.md`, `setlayout.md`, `focus.md` analysis:
- `Pertag` struct stores arrays indexed by tag number (1..N plus 0 for "all")
- `view()` swaps `seltags` (active slot) and restores all per-tag settings
- `tagset[2]` — two arrays, `seltags` is the active index (0 or 1)
- `curtag` / `prevtag` track current/previous tag for toggle
- `pertag->sel[tag]` stores focused client per tag

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_TAG_VIEW_CHANGED` | View/tag switched | `Monitor*`, `old_tag`, `new_tag`, `old_tagset`, `new_tagset` |
| `EVT_LAYOUT_CHANGED` | Layout changed | `Monitor*`, `slot`, `new_layout` |
| `EVT_TILING_PARAM_CHANGED` | mfact or nmaster changed | `Monitor*`, `param`, `value` |
| `EVT_CLIENT_FOCUS_GAIN` | Client gained focus | `Client*`, `Monitor*` |
| `EVT_BAR_SHOWN/HIDDEN` | Bar visibility changed | `Monitor*` |

## Hook Points

```c
typedef struct {
    // Per-tag state (index 0 = all tags, 1..N = specific tags)
    Layout* layouts[NUM_TAGS + 1][2];      // Two layout pointers per tag
    float mfacts[NUM_TAGS + 1];           // mfact per tag
    int nmasters[NUM_TAGS + 1];           // nmaster per tag
    uint8_t sellts[NUM_TAGS + 1];         // sellt (layout slot) per tag
    int showbars[NUM_TAGS + 1];           // bar visibility per tag
    Client* focused[NUM_TAGS + 1];        // focused client per tag
    
    // Current state
    int curtag;                           // Current tag index (0 = all)
    int prevtag;                          // Previous tag for toggle
} PertagContext;

// Hook: Called when tag changes — save old and restore new
void on_tag_view_changed(const Event* evt, void* userdata) {
    PertagContext* ctx = userdata;
    Monitor* mon = evt->source;
    
    // Save current settings to prevtag
    ctx->layouts[ctx->prevtag][mon->sellt] = mon->lt[mon->sellt];
    ctx->mfacts[ctx->prevtag] = mon->mfact;
    ctx->nmasters[ctx->prevtag] = mon->nmaster;
    ctx->sellts[ctx->prevtag] = mon->sellt;
    ctx->focused[ctx->prevtag] = mon->sel;
    
    // Restore settings for new tag
    mon->lt[mon->sellt] = ctx->layouts[evt->new_tag][mon->sellt];
    mon->mfact = ctx->mfacts[evt->new_tag];
    mon->nmaster = ctx->nmasters[evt->new_tag];
    mon->sellt = ctx->sellts[evt->new_tag];
    mon->sel = ctx->focused[evt->new_tag];
    
    ctx->curtag = evt->new_tag;
    ctx->prevtag = evt->old_tag;
}
```

## Configuration

```c
typedef struct {
    int enabled;
    int save_focused_client;   // Remember which client was focused per tag
    int save_layout;           // Remember layout per tag
    int save_mfact;           // Remember mfact per tag
    int save_nmaster;         // Remember nmaster per tag
    int save_bar;             // Remember bar visibility per tag
} PertagConfig;
```

## Keybindings (from dwm config)

```c
// view() is called with specific tag bits → saved to that tag
{MODKEY, XK_1, view, {.ui = 1 << 0}},
{MODKEY, XK_2, view, {.ui = 1 << 1}},
// ...

// view(NULL) → toggle to previous tag (swaps prevtag/curtag)
{MODKEY, XK_Tab, view, {0}},

// toggleview → toggle tag visibility (multi-tag view)
{MODKEY | ControlMask, XK_1, toggleview, {.ui = 1 << 0}},
```

## Implementation Notes

- Tag 0 is special: "all tags" mode (`curtag == 0`)
- Arrays are sized `LENGTH(tags) + 1` to accommodate index 0
- `curtag = 0` means no specific tag (view all)
- When `arg->ui == ~0` (view all), `curtag = 0`
- The core `view()` function handles tag switching; pertag hooks into the transition

## Interactions

- Works with all layouts (tile, float, monocle, bstack)
- Works with gap plugin (pertag also stores gap settings)
- When client is destroyed, `pertag->sel[]` must be cleared for that client

---

*Extension ID: pertag*
*Priority: High (dwm staple feature)*
*DWM patch equivalent: dwmpertag*