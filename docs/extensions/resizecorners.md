# Extension: Resize Corners

## Overview

Adds corner-based resizing for floating windows. Instead of only edge-based resizing, allows dragging from any corner to resize while maintaining aspect ratio or anchoring the opposite corner.

## DWM Reference

Based on `resizemouse.md`:
- `resizemouse()` already detects corner from pointer position
- `horizcorner` and `vertcorner` determine which edge(s) are dragged
- Corner dragging: `nw = ocx2 - nx` (left edge moves) vs `nw = ev.x - ocx` (right edge moves)
- Minimum size enforced via `c->minw, c->minh`

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_RESIZE_START` | Resize operation started | `Client*`, `corner`, `Monitor*` |
| `EVT_RESIZE_UPDATE` | Resize position updated | `Client*`, `x`, `y`, `w`, `h` |
| `EVT_RESIZE_END` | Resize operation ended | `Client*` |

## Hook Points

```c
typedef struct {
    int snap_to_edges;          // Snap to other windows' edges
    int snap_threshold;         // Pixels before snap triggers
    int maintain_aspect;        // Maintain aspect ratio on corner drag
    int preserve_center;        // Keep center when resizing from edges
} ResizeConfig;

void on_resize_update(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    ResizeConfig* cfg = userdata;
    
    if (cfg->maintain_aspect && evt->corner != CORNER_NONE) {
        // Maintain original aspect ratio
        float aspect = (float)c->w / c->h;
        // Adjust height based on width change
    }
    
    if (cfg->snap_to_edges) {
        // Check for snap points (other windows, monitor edges)
        check_and_apply_snap(c, cfg->snap_threshold);
    }
}
```

## Corner Constants

```c
enum Corner {
    CORNER_NONE = 0,
    CORNER_TOP_LEFT,
    CORNER_TOP_RIGHT,
    CORNER_BOTTOM_LEFT,
    CORNER_BOTTOM_RIGHT,
    CORNER_TOP_EDGE,
    CORNER_BOTTOM_EDGE,
    CORNER_LEFT_EDGE,
    CORNER_RIGHT_EDGE,
};
```

## Interactions

- Works with floating clients only (tiled uses layout)
- Can trigger `snap_to_float` auto-float during resize
- Works with `floatpos` for initial positioning

---

*Extension ID: resizecorners*
*Priority: Low (nice to have)*
*DWM patch equivalent: dwm-resizecorners*