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

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_TILING_PARAM_CHANGED` | Tiling parameters updated | `Monitor*`, `param_type`, `old_val`, `new_val` |
| `EVT_LAYOUT_CHANGED` | Layout switched | `Monitor*`, `old_layout`, `new_layout` |
| `EVT_TAG_VIEW_CHANGED` | Tag/view switched | `Monitor*`, `old_tag`, `new_tag` |

## Hook Points

```c
// Plugin subscribes to these events
typedef struct {
    int gap_size;        // Current gap in pixels
    int enabled;         // Gaps enabled/disabled
    int mode;            // 0=singular borders, 1=full gaps
} GapPluginConfig;

// Hook callbacks
void on_tiling_param_changed(const Event* evt, void* userdata) {
    GapPluginConfig* cfg = userdata;
    Monitor* mon = evt->source;
    // Update monitor's gap size from config
    mon->gap_size = cfg->gap_size;
}

void on_layout_changed(const Event* evt, void* userdata) {
    // Apply gaps based on current mode
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

## Implementation Notes

- Gaps stored in `Monitor.gap_size` (per-monitor)
- Per-tag: `Pertag.gappx[]` array stores per-tag values
- Two tiling algorithms: "singular borders" (original) vs "full gaps" (separate all sides)
- The plugin reads config and sets monitor values; the core tiling applies them
- No modification to `tile()` function needed

## Interactions

- Works with all tiling layouts (tile, monocle, bstack)
- Does not affect floating clients (they have their own positioning)
- Bar height auto-adjusts if gaps affect window area

---

*Extension ID: gap*
*Priority: High (very common feature)*
*DWM patch equivalent: dwm-gaps*