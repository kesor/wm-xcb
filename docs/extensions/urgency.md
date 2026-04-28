# Extension: Urgency Notifications

## Overview

Handles client urgency hints (WM_HINTS urgency flag) by:
1. Visual indication in the status bar (urgent client's tag lights up)
2. Optional audio notification when urgency is set
3. Auto-focus option when urgent client becomes visible
4. Optional flashing border on urgent clients

## DWM Reference

Based on `focus.md`, `clientmessage.md`, `seturgent()`:
- `isurgent` flag in `Client` struct
- `updatewmhints()` reads WM_HINTS, sets urgency
- `seturgent(c, 1)` sets urgency flag and updates WM hints
- `seturgent(c, 0)` clears when client gains focus
- Bar draws urgency indicator (box) for tags with urgent clients
- `clientmessage()` sets urgency for `_NET_ACTIVE_WINDOW` requests

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_CLIENT_URGENCY_SET` | Client became urgent | `Client*` |
| `EVT_CLIENT_URGENCY_CLEAR` | Client urgency cleared | `Client*` |
| `EVT_TAG_VIEW_CHANGED` | View switched — check for urgent | `Monitor*` |
| `EVT_CLIENT_VISIBLE` | Urgent client became visible | `Client*` |

## Hook Points

```c
typedef struct {
    const char* sound_file;       // Path to sound file (e.g., "/usr/share/sounds/notify.wav")
    int play_sound;              // Enable sound notifications
    int auto_focus;              // Auto-focus urgent client when visible
    int flash_border;            // Flash border when urgent
    int flash_duration_ms;       // Border flash duration
} UrgencyConfig;

void on_urgency_set(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    UrgencyConfig* cfg = userdata;
    
    if (cfg->play_sound && cfg->sound_file) {
        // Play notification sound in background
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "aplay '%s' &", cfg->sound_file);
        system(cmd);
    }
    
    if (cfg->flash_border) {
        // Start border flash animation
        start_border_flash(c, cfg->flash_duration_ms);
    }
    
    LOG_INFO("Client %s marked as urgent", c->name);
}

void on_tag_view_changed(const Event* evt, void* userdata) {
    if (!userdata->auto_focus) return;
    
    Monitor* mon = evt->source;
    
    // Find urgent client on new view
    for (Client* c = mon->clients; c; c = c->next) {
        if (c->isurgent && (c->tags & mon->tagset[mon->seltags])) {
            // Focus the urgent client
            focus(c);
            break;
        }
    }
}
```

## Configuration

```c
static UrgencyConfig urgency_config = {
    .sound_file = "/usr/share/sounds/notification.wav",
    .play_sound = 1,
    .auto_focus = 0,           // Don't auto-focus by default
    .flash_border = 1,
    .flash_duration_ms = 500,
};
```

## Bar Indicators

The bar shows urgency via:
- Small square indicator on urgent tags in tag list
- Background color change for urgent tags
- `urg` bitmask in `drawbar()`: `urg |= c->tags` for all urgent clients

```c
// In drawbar(), urgency indicators
if (urg & (1 << i)) {
    // Draw urgency indicator (box)
    drw_rect(drw, x + boxw, 0, w - 2*boxw, boxw, 0, 1);
}
```

## Interactions

- Works with all layouts
- Urgency state persists across tag switches
- When urgent client loses focus (unfocus), `isurgent` stays set
- Only cleared when client gains focus (`focus()` calls `seturgent(c, 0)`)
- Multiple urgent clients: all indicators shown

---

*Extension ID: urgency*
*Priority: Medium (nice to have)*
*DWM patch equivalent: dwm-urgencyhook / dwm-systray integration*