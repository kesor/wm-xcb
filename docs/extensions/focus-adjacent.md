# Extension: Focus Adjacent Tag

## Overview

Allows navigating to the next/previous tag in cyclic order. When at the leftmost tag and going left, wraps to the rightmost tag, and vice versa. Similar to vim's tag navigation.

## DWM Reference

Based on `tagtoleft.md`, `tagtoright.md`, `viewtoleft.md`, `viewtoright.md`:
- `tagtoleft()` shifts client's tags left: `tags >>= 1`
- `tagtoright()` shifts client's tags right: `tags <<= 1`
- `viewtoleft()` shifts view left: single bit → shift left
- `viewtoright()` shifts view right: single bit → shift right
- Current constraint: only works when single tag is active

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_TAG_VIEW_CHANGED` | View changed via navigation | `Monitor*`, `old_tag`, `new_tag` |

## Hook Points

```c
typedef struct {
    int wrap_around;           // Wrap at edges
    int move_client_with_tag;  // Also move focused client
} FocusAdjacentConfig;

void view_left(Monitor* mon) {
    unsigned int cur = mon->tagset[mon->seltags];
    
    // Find leftmost set bit
    if (cur == 1) {  // Tag 1 is active
        if (wrap_around)
            cur = 1 << (NUM_TAGS - 1);  // Wrap to last tag
        else
            return;  // Can't go further left
    } else {
        cur >>= 1;  // Shift right (go left in tag order)
    }
    
    Arg arg = { .ui = cur };
    view(&arg);
}

void view_right(Monitor* mon) {
    unsigned int cur = mon->tagset[mon->seltags];
    
    if (cur & (1 << (NUM_TAGS - 1))) {  // Last tag active
        if (wrap_around)
            cur = 1;  // Wrap to first tag
        else
            return;
    } else {
        cur <<= 1;  // Shift left (go right in tag order)
    }
    
    Arg arg = { .ui = cur };
    view(&arg);
}
```

## Keybindings

```c
static Key keys[] = {
    { MODKEY, XK_Left, viewtoleft, {0} },
    { MODKEY, XK_Right, viewtoright, {0} },
    { MODKEY | ShiftMask, XK_Left, tagtoleft, {0} },
    { MODKEY | ShiftMask, XK_Right, tagtoright, {0} },
    { ALTMOD, XK_comma, focusmon, {.i = -1} },  // Previous monitor
    { ALTMOD, XK_period, focusmon, {.i = +1} },   // Next monitor
};
```

## Interactions

- Works with `pertag` (view changes trigger pertag)
- Works with all layouts
- Doesn't affect multi-tag views (only works with single tag)

---

*Extension ID: focus-adjacent*
*Priority: Low (convenience feature)*
*DWM patch equivalent: dwm-focusadjacenttag*