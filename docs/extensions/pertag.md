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

| Event | Direction | Description |
|-------|-----------|-------------|
| `EVT_TAG_VIEW_CHANGED` | Subscribes + Emits | View/tag switched |
| `EVT_LAYOUT_CHANGED` | Subscribes | Layout changed |
| `EVT_TILING_PARAM_CHANGED` | Subscribes | mfact or nmaster changed |
| `EVT_CLIENT_FOCUS_GAIN` | Subscribes | Client gained focus |
| `EVT_BAR_SHOWN/HIDDEN` | Subscribes | Bar visibility changed |

## Component Design

**Target type:** `TARGET_TYPE_MONITOR`, `TARGET_TYPE_TAG`

**Provides:** PertagSM (stores per-tag arrays per monitor)

**Subscribes to:** `EVT_LAYOUT_CHANGED`, `EVT_TILING_PARAM_CHANGED`, `EVT_CLIENT_FOCUS_GAIN`

**Emits:** `EVT_TAG_VIEW_CHANGED`

```c
Component pertag_component = {
    .name = "pertag",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_MONITOR },
    .requests = (RequestType[]){ REQ_TAG_VIEW },
    .events = (EventType[]){ EVT_TAG_VIEW_CHANGED },
    .on_init = pertag_on_init,
    .executor = pertag_executor,
    .get_sm_template = pertag_get_template,
};
```

## Hooks

```c
void pertag_on_init(void) {
    hub_subscribe(EVT_LAYOUT_CHANGED, on_layout_changed, NULL);
    hub_subscribe(EVT_TILING_PARAM_CHANGED, on_tiling_param_changed, NULL);
    hub_subscribe(EVT_CLIENT_FOCUS_GAIN, on_client_focus_gain, NULL);
}

// Hook: Called when tag changes — save old and restore new
void pertag_on_tag_view_changed(const Event* evt, void* userdata) {
    PertagContext* ctx = userdata;
    Monitor* mon = hub_get_monitor(evt->target);
    
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

## Keybindings

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

## Interactions with Other Components

| Component | Interaction |
|-----------|-------------|
| **Tiling layouts** | Restores `mon->lt[]`, `mon->mfact`, `mon->nmaster` per tag |
| **Focus** | Restores `mon->sel` (focused client) per tag |
| **Bar** | Restores bar visibility per tag |
| **Gap** | Stores gap settings in pertag arrays |
| **EWMH desktop** | Responds to `EVT_TAG_VIEW_CHANGED` for `_NET_CURRENT_DESKTOP` |

---

*Extension ID: pertag*
*Priority: High (dwm staple feature)*
*DWM patch equivalent: dwmpertag*
*Hub: Registers with Hub, subscribes to events, emits EVT_TAG_VIEW_CHANGED*
