# Extension: Actual Fullscreen

## Overview

A fullscreen mode that respects window borders and bar visibility, unlike dwm's default fullscreen which hides both. "Actual fullscreen" fills the entire monitor including the bar area, while still allowing EWMH compliance.

## DWM Reference

Based on `setfullscreen.md`, `togglefloating.md`:
- `setfullscreen()` enters fullscreen, sets `isfullscreen = 1`
- Fullscreen implies `isfloating = 1` (borders suppressed)
- `bw = 0` for fullscreen clients
- Bar hidden on fullscreen via `togglebar()` in `togglefullscr()`
- Exit fullscreen restores `oldstate`, `oldbw`, geometry

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_CLIENT_FULLSCREEN_ENTER` | Client entered fullscreen | `Client*` |
| `EVT_CLIENT_FULLSCREEN_EXIT` | Client exited fullscreen | `Client*` |
| `EVT_BAR_SHOWN/HIDDEN` | Bar visibility changed | `Monitor*` |

## Two Fullscreen Modes

| Mode | Bar | Border | X11 Geometry |
|------|-----|--------|--------------|
| dwm Default | Hidden | 0 | Monitor size (mx, my, mw, mh) |
| Actual Fullscreen | Visible | 0 | Full screen including bar |

The extension adds an `actualfullscreen` flag per client:

```c
typedef struct {
    int actual_fullscreen;      // If set, include bar in fullscreen
    int preserve_bar_state;    // Remember bar state for restore
} ActualFullscreenConfig;
```

## Hook Points

```c
void on_fullscreen_enter(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    ActualFullscreenConfig* cfg = userdata;
    
    if (cfg->actual_fullscreen) {
        // Resize to include bar area
        Monitor* m = c->mon;
        resizeclient(c, m->mx, m->my, m->mw, m->mh);  // Full physical screen
        
        // Keep bar visible (don't hide)
    } else {
        // Standard dwm behavior: resize to window area, hide bar
        resizeclient(c, m->mx, m->my, m->mw, m->mh);  // Excludes bar
        togglebar(NULL);  // Hide bar
    }
}

void on_fullscreen_exit(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    ActualFullscreenConfig* cfg = userdata;
    
    // Restore bar if it was hidden
    if (cfg->preserve_bar_state && c->mon->oldbar) {
        togglebar(NULL);
    }
}
```

## Configuration

```c
static ActualFullscreenConfig afs_config = {
    .actual_fullscreen = 1,    // Enable actual fullscreen mode
    .preserve_bar_state = 1,   // Remember and restore bar state
};
```

## Keybindings

```c
static Key keys[] = {
    // Toggle actual fullscreen (separate from standard fullscreen)
    { MODKEY | ShiftMask, XK_f, actualfullscreen_toggle, {0} },
};
```

## Interactions

- Can work alongside standard fullscreen as separate mode
- EWMH `_NET_WM_STATE_FULLSCREEN` can trigger either mode
- Respects `lockfullscreen` config option
- Bar visibility managed separately from fullscreen state

---

*Extension ID: actualfullscreen*
*Priority: Medium (popular enhancement)*
*DWM patch equivalent: dwm-actualfullscreen*
