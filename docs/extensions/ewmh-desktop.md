# Extension: EWMH Desktop Tags

## Overview

Publishes the tag model as EWMH desktops, so a panel or pager that knows
nothing about this WM can find it, list the windows it manages, name the
desktops, and switch between them.

The component is built in: `src/components/ewmh-desktop.c`, registered as
`ewmh-desktop` by `wm_components_init()`. It owns no state machine. Tag state
belongs to the tag manager, and this component reads it from the events the tag
manager emits.

## DWM Reference

Based on `view.md`, `clientmessage.md`:
- `setnumdesktops()`, `setdesktopnames()`, `setcurrentdesktop()`, `setviewport()` in dwm
- `updatecurrentdesktop()` called after tag changes
- `_NET_DESKTOP_NAMES` is a UTF8 text property with the tag names
- An external app asks for `_NET_CURRENT_DESKTOP` with a ClientMessage

Where this differs from dwm: the tag view can show several tags, and EWMH names
one desktop. The current desktop is the lowest visible tag, and a client on more
than one tag reports `_NET_WM_DESKTOP` as `0xFFFFFFFF`, which EWMH defines as
"on every desktop". dwm's EWMH patch writes the client's raw tag bitmask, which
a panel reads as a desktop index that does not exist.

## Root Window Properties

| Property | Type | Value |
|----------|------|-------|
| `_NET_SUPPORTING_WM_CHECK` | WINDOW | a window the WM creates, which carries the same property pointing at itself, and `_NET_WM_NAME` = `wm-xcb` |
| `_NET_SUPPORTED` | ATOM | the atoms this component sets |
| `_NET_NUMBER_OF_DESKTOPS` | CARDINAL | the tag count, `TAG_NUM_TAGS` |
| `_NET_DESKTOP_NAMES` | UTF8_STRING | the tag names from the config file, NUL-separated; a tag with no name is its number |
| `_NET_DESKTOP_VIEWPORT` | CARDINAL | all zero: a desktop here is a tag, not a scroll position |
| `_NET_CURRENT_DESKTOP` | CARDINAL | the lowest visible tag of the selected monitor |
| `_NET_CLIENT_LIST` | WINDOW | the managed windows, in the order they were mapped |

## Client Window Properties

| Property | Type | Value |
|----------|------|-------|
| `_NET_WM_DESKTOP` | CARDINAL | the client's tag index, or `0xFFFFFFFF` when it is on several tags or none |

## What Keeps the Properties Current

| Source | Effect |
|--------|--------|
| `EVT_TAG_CHANGED` with a visible mask | rewrite `_NET_CURRENT_DESKTOP`; rewrite every client's `_NET_WM_DESKTOP` |
| `EVT_TAG_CHANGED` about a client | rewrite every client's `_NET_WM_DESKTOP` |
| `EVT_CLIENT_MANAGED` | write that client's `_NET_WM_DESKTOP`, rewrite `_NET_CLIENT_LIST` |
| `EVT_CLIENT_UNMANAGED`, `EVT_CLIENT_DESTROYED` | rewrite `_NET_CLIENT_LIST` without the window the event names |
| `XCB_CLIENT_MESSAGE` with `_NET_CURRENT_DESKTOP` | send `REQ_TAG_VIEW` for that desktop's tag to the selected monitor |

The last row is the whole request path: a pager's desktop change goes through
the same request a keybinding sends, so the tag manager decides whether it
happens, and the events above then carry the result back out. This component
never sets a tag itself and never calls the tag manager.

A request for a desktop at or above the tag count, and a ClientMessage of any
other type, are logged and ignored.

## Configuration

There is no separate configuration. `_NET_NUMBER_OF_DESKTOPS` and
`_NET_DESKTOP_NAMES` come from `TAG_NUM_TAGS` and the `tags` line of the config
file (`docs/configuration.md`).

## What This Does Not Do Yet

- `_NET_WM_DESKTOP` as a client message: a pager can ask to switch desktops, not
  to move a window between them.
- `_NET_ACTIVE_WINDOW`, `_NET_CLOSE_WINDOW`, `_NET_WM_STATE` messages from a
  panel.
- A tag name changed at runtime does not rewrite `_NET_DESKTOP_NAMES`.
- `_NET_DESKTOP_VIEWPORT` is written but never anything other than zero.

## Interactions

- Required for external panels and pagers (polybar, waybar, rofi, xfce-panel)
- Follows the tag manager's view, so it reflects `pertag` for the selected
  monitor
- A client that is hidden by a tag switch keeps its place in `_NET_CLIENT_LIST`:
  the window is still managed, and EWMH has no "not on screen now"

---

*Extension ID: ewmh-desktop*
*Priority: High (interoperability)*
*DWM patch equivalent: dwm-ewmh*
