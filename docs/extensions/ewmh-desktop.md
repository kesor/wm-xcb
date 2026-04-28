# Extension: EWMH Desktop Tags

## Overview

Provides EWMH-compliant desktop/tag management, allowing external tools (pagers, panels) to interact with the window manager's workspace system. This exposes tag state via `_NET_NUMBER_OF_DESKTOPS`, `_NET_DESKTOP_NAMES`, `_NET_CURRENT_DESKTOP`, etc.

## DWM Reference

Based on `view.md`, `clientmessage.md`:
- `setnumdesktops()`, `setdesktopnames()`, `setcurrentdesktop()`, `setviewport()` in dwm
- `updatecurrentdesktop()` called after tag changes
- `_NET_DESKTOP_NAMES` uses UTF8 text property with tag names
- External apps can request `_NET_CURRENT_DESKTOP` change

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_TAG_VIEW_CHANGED` | View changed | `Monitor*`, `tagset` |
| `EVT_TAG_NAME_CHANGED` | Tag name modified | `tag_index`, `new_name` |
| `EVT_EWMH_DESKTOP_REQUEST` | External desktop change request | `desktop_index` |

## EWMH Atoms

```c
// Atoms that this extension manages
static Atom netatom[] = {
    [NetNumberOfDesktops] = "_NET_NUMBER_OF_DESKTOPS",
    [NetDesktopNames] = "_NET_DESKTOP_NAMES",
    [NetCurrentDesktop] = "_NET_CURRENT_DESKTOP",
    [NetDesktopViewport] = "_NET_DESKTOP_VIEWPORT",
};
```

## Hook Points

```c
typedef struct {
    const char** tag_names;     // Tag display names
    int num_tags;               // Number of tags
    int ewmh_support;           // Enable EWMH support
} EwmhConfig;

void on_tag_view_changed(const Event* evt, void* userdata) {
    // Update _NET_CURRENT_DESKTOP
    Monitor* mon = evt->source;
    unsigned int tagset = mon->tagset[mon->seltags];
    
    // Find index of active tag (assuming single tag)
    int idx = 0;
    unsigned int mask = 1;
    for (int i = 0; i < num_tags; i++) {
        if (tagset & mask) {
            idx = i;
            break;
        }
        mask <<= 1;
    }
    
    // Send to root window
    long data = idx;
    XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char*)&data, 1);
}

void on_ewmh_desktop_request(const Event* evt, void* userdata) {
    // Handle _NET_CURRENT_DESKTOP from external app
    int requested = evt->desktop_index;
    if (requested >= 0 && requested < num_tags) {
        Arg arg = { .ui = 1 << requested };
        view(&arg);
    }
}
```

## Configuration

```c
static EwmhConfig ewmh_config = {
    .tag_names = (const char*[]){"1", "2", "3", "4", "5", "6", "7", "8", "9"},
    .num_tags = 9,
    .ewmh_support = 1,
};
```

## Root Window Properties

| Property | Type | Description |
|----------|------|-------------|
| `_NET_NUMBER_OF_DESKTOPS` | CARDINAL | Number of tags/desktops |
| `_NET_DESKTOP_NAMES` | UTF8_STRING | Tag names (null-separated) |
| `_NET_CURRENT_DESKTOP` | CARDINAL | Currently active tag index |
| `_NET_DESKTOP_VIEWPORT` | CARDINAL | Viewport position (for virtual desktops) |
| `_NET_SUPPORTED` | ATOM | List of supported EWMH atoms |

## Interactions

- Required for external panels/pagers (e.g., polybar, xfce-panel)
- Works with `pertag` (EWMH reflects current view)
- Tag names can be changed at runtime

---

*Extension ID: ewmh-desktop*
*Priority: High (interoperability)*
*DWM patch equivalent: dwm-ewmh*