# Extension: Bar Padding and Styling

## Overview

Configurable status bar appearance including padding, position (top/bottom), and visual styling. This extends the default bar with more customization options.

## DWM Reference

Based on `globals.md`, `togglebar.md`:
- `bh` = bar height (font height + vertical padding)
- `sp` = side padding
- `lrpad` = left/right padding for text
- `vp` = vertical padding (adjusts for top/bottom bar)
- `topbar` = bar position (1=top, 0=bottom)
- `showbar` = current visibility
- `vertpadbar` = vertical padding of bar
- `horizpadbar` = horizontal padding for status text

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_BAR_SHOWN` | Bar became visible | `Monitor*` |
| `EVT_BAR_HIDDEN` | Bar became hidden | `Monitor*` |
| `EVT_BAR_DRAW` | Bar being drawn | `Monitor*` |

## Configuration

```c
typedef struct {
    int height;                 // Bar height in pixels
    int side_pad;              // Horizontal padding (left/right of bar)
    int vert_pad;              // Vertical padding between bar and window area
    int horiz_pad_bar;         // Horizontal padding for bar content
    int top_bar;               // 1=top, 0=bottom
    int show_by_default;       // Show bar on startup
} BarStyleConfig;

static BarStyleConfig bar_config = {
    .height = 0,               // 0 = auto (font height + vert_pad_bar)
    .side_pad = 2,
    .vert_pad = 2,
    .horiz_pad_bar = 2,
    .top_bar = 1,
    .show_by_default = 1,
};
```

## Hook Points

```c
void on_bar_shown(const Event* evt, void* userdata) {
    Monitor* m = evt->source;
    BarStyleConfig* cfg = userdata;
    
    // Update bar window geometry
    update_bar_geometry(m, cfg);
    
    // Map the bar window
    xcb_map_window(dpy, m->barwin);
}

void on_bar_draw(const Event* evt, void* userdata) {
    Monitor* m = evt->source;
    BarStyleConfig* cfg = userdata;
    
    // Draw with configured padding
    int x = cfg->side_pad;
    int w = m->ww - 2 * cfg->side_pad;
    
    // Draw tags, layout symbol, client name, etc.
    draw_tags(m, x, w, cfg);
    draw_layout_symbol(m, ...);
    draw_client_name(m, ...);
    draw_status_text(m, ...);
}
```

## Keybindings

```c
static Key keys[] = {
    { MODKEY, XK_b, togglebar, {0} },
};
```

## Bar Window Updates

```c
void update_bar_geometry(Monitor* m, BarStyleConfig* cfg) {
    int bh = cfg->height ? cfg->height : font_height + 2 * cfg->vert_pad;
    
    if (cfg->top_bar) {
        m->by = m->wy;              // Bar at top of work area
        m->wy = m->wy + bh;         // Work area below bar
    } else {
        m->by = m->wy + m->wh;      // Bar at bottom
        m->wh = m->wh - bh;        // Work area above bar
    }
    
    // Resize bar window
    xcb_set_window_rect(dpy, m->barwin, m->wx + cfg->side_pad, m->by,
                        m->ww - 2 * cfg->side_pad, bh);
}
```

## Interactions

- Affects window area calculation (`wy, wh`)
- Works with fullscreen mode (bar hidden/restored)
- Works with `pertag` (bar visibility per tag)

---

*Extension ID: bar-style*
*Priority: Medium (common customization)*
*DWM patch equivalent: dwm-bar-padding, dwm-statuspadding*