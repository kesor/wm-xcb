# Extension: BStack Layout (Bottom Stack)

## Overview

A tiling layout variant where the master area is at the top and the stack area fills the bottom portion of the screen. Two sub-variants:
- **BStack**: Master at top, stack below (horizontal slices)
- **BStackHoriz**: Same but stack uses horizontal splits instead of vertical columns

## DWM Reference

Based on `tile.md` analysis:
- `bstack()` and `bstackhoriz()` functions in dwm
- BStack: master on top (full width), stack in columns below
- BStackHoriz: master on top, stack rows below
- Both use `nmaster` to determine how many clients in master area
- `mfact` controls master height/width split

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_LAYOUT_CHANGED` | Layout switched to bstack | `Monitor*`, `layout` |
| `EVT_LAYOUT_ARRANGE` | Layout arranges clients | `Monitor*`, `layout` |

## Layout Function Signature

```c
typedef void (*ArrangeFn)(Monitor* m);

typedef struct Layout {
    const char* symbol;        // Display symbol in bar (e.g., "TTT")
    ArrangeFn arrange;        // Tiling function, or NULL for floating
} Layout;

static Layout layouts[] = {
    { .symbol = "[]=", .arrange = tile },
    { .symbol = "><>", .arrange = NULL },  // Floating
    { .symbol = "[M]", .arrange = monocle },
    { .symbol = "TTT", .arrange = bstack },
    { .symbol = "===", .arrange = bstackhoriz },
};
```

## BStack Algorithm

```c
void bstack(Monitor* m) {
    unsigned int i, n, h, mw, my;
    Client* c;
    
    // Count visible clients
    for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
    if (n == 0) return;
    
    // Determine master height
    if (n > m->nmaster)
        mw = m->mfact * m->wh;  // Master gets mfact fraction
    else
        mw = m->wh;              // All in master if fewer than nmaster
    
    // Master area (top)
    h = mw;
    my = 0;
    for (i = 0, c = nexttiled(m->clients); c && i < m->nmaster; c = nexttiled(c->next)) {
        resize(c, m->wx, m->wy + my, m->ww, h - 2*c->bw, 0);
        my += HEIGHT(c);
        i++;
    }
    
    // Stack area (bottom) — vertical columns
    for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
        if (i < m->nmaster) {
            i++; continue;
        }
        // Stack columns below master
        resize(c, m->wx, m->wy + mw, m->ww, m->wh - mw - 2*c->bw, 0);
    }
}
```

## Hook Points

```c
void on_layout_arrange_begin(const Event* evt, void* userdata) {
    if (evt->layout == LAYOUT_BSTACK || evt->layout == LAYOUT_BSTACK_HORIZ) {
        // Pre-arrangement setup for bstack
    }
}

void on_layout_arrange_end(const Event* evt, void* userdata) {
    // Post-arrangement (e.g., update bar symbol with client count)
}
```

## Configuration

```c
static LayoutConfig layout_config = {
    .default_layout = &layouts[0],  // Tile
    .bstack_cols = 0,               // 0 = automatic columns
    .bstack_stretch = 1,           // Allow nmaster > visible clients
};
```

## Keybindings

```c
static Key keys[] = {
    { MODKEY, XK_u, setlayout, {.v = &layouts[3]} },  // BStack
    { MODKEY, XK_o, setlayout, {.v = &layouts[4]} },  // BStackHoriz
};
```

## Interactions

- Uses `nmaster` and `mfact` like `tile()`
- Respects `cfact` (client factor) for relative sizing
- Works with `pertag` (layout saved per tag)
- Works with `gap` component (gaps applied in resize calls)
- Floating clients excluded from tiling (`nexttiled()`)

---

*Extension ID: bstack*
*Priority: Medium (alternative layout)*
*DWM patch equivalent: dwm-bstack*