# Extension: Gap Plugin

## Overview

Adds configurable gaps between tiled windows. When enabled, each window has padding on all sides (top, bottom, left, right), creating a visual separation between windows. The gap size can be configured per-tag and toggled on/off.

## DWM Reference

Based on `tile.md` and `setmfact.md` analysis:
- dwm has `gappx[]` array in `Pertag` struct
- `tile()` checks `drawwithgaps` flag and applies gaps per-client
- `setgaps()` modifies gaps via GAP_TOGGLE (100), GAP_RESET (0), or delta
- Two modes: "single borders" (borders between clients) vs "full gaps" (all sides)

## State Machine Events

| Event | Direction | Description |
|-------|-----------|-------------|
| `EVT_TILING_PARAM_CHANGED` | Subscribes | Tiling parameters updated |
| `EVT_LAYOUT_CHANGED` | Subscribes | Layout switched |
| `EVT_TAG_VIEW_CHANGED` | Subscribes | Tag/view switched |

This component **subscribes to events** — it does not emit state machine events.

## Component Design

**Target type:** `TARGET_TYPE_MONITOR`

**Provides:** GapSM (tracks gap size per monitor). The SM stores configuration; the tiling component reads `Monitor.gap_size` when arranging windows.

**Subscribes to:** `EVT_TILING_PARAM_CHANGED`, `EVT_LAYOUT_CHANGED`, `EVT_TAG_VIEW_CHANGED`

```c
Component gap_component = {
    .name = "gap",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_MONITOR },
    .requests = (RequestType[]){ REQ_MONITOR_SET_GAP },
    .events = (EventType[]){ /* none */ },
    .on_init = gap_on_init,
    .executor = gap_executor,
};
```

## Hooks

```c
// Gap subscribes to these events via Hub
void gap_on_init(void) {
    hub_subscribe(EVT_TILING_PARAM_CHANGED, on_tiling_param_changed, &gap_config);
    hub_subscribe(EVT_LAYOUT_CHANGED, on_layout_changed, &gap_config);
    hub_subscribe(EVT_TAG_VIEW_CHANGED, on_tag_view_changed, &gap_config);
}

void on_tiling_param_changed(const Event* evt, void* userdata) {
    GapConfig* cfg = userdata;
    HubTarget* target = hub_get_target_by_id(evt->target);
    if (target && target->type == TARGET_TYPE_MONITOR) {
        Monitor* mon = (Monitor*)target;
        mon->gap_size = cfg->gap_size;
    }
}
```

## Configuration

```c
// config.h
#define DEFAULT_GAP_SIZE 10
#define DEFAULT_DRAW_WITH_GAPS 1

typedef struct {
    int gap_size;           // Pixels between windows
    int show_gaps;          // Boolean: gaps visible
    int draw_with_gaps;     // 0=single borders, 1=full gaps
    int toggleable;         // Can be toggled via keybind
} GapConfig;
```

## Keybindings

```c
static Key keys[] = {
    { MODKEY, XK_g, gap_toggle, {.i = GAP_TOGGLE} },      // Toggle gaps
    { MODKEY, XK_0, gap_reset, {.i = GAP_RESET} },        // Reset to default
    { MODKEY | ShiftMask, XK_g, gap_inc, {.i = +5} },      // Increase
    { MODKEY | ControlMask, XK_g, gap_dec, {.i = -5} },    // Decrease
};
```

## Interactions with Other Components

| Component | Interaction |
|-----------|-------------|
| **Tiling layout** | Reads `Monitor.gap_size` when arranging windows |
| **Pertag** | Uses pertag's per-tag arrays for gap storage |
| **Bar** | Adjusts bar position if gaps affect window area |
| **Floating** | Does not affect floating clients |

---

*Extension ID: gap*
*Priority: High (very common feature)*
*DWM patch equivalent: dwm-gaps*
*Hub: Registers with Hub, subscribes to events, provides executor*
