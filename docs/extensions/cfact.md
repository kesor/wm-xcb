# Extension: CFact (Client Factor / Relative Window Sizing)

## Overview

Adds per-client "factor" values that control relative sizing in the tiling layout. Each client has a `cfact` value (default 1.0) that scales its portion of the master or stack area. This allows some windows to be larger or smaller than others without changing mfact.

## DWM Reference

Based on `tile.md` and `setcfact.md`:
- `cfact` field in `Client` struct (float)
- `tile()` accumulates mfacts/sfacts from `c->cfact`
- Height computed as: `(wh - my) * (cfact / mfacts)`
- `setcfact()` adjusts cfact with delta or absolute value
- Valid range: [0.25, 4.0]

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_CLIENT_CFACT_CHANGED` | Client factor updated | `Client*`, `old_cfact`, `new_cfact` |
| `EVT_TILING_RECALCULATE` | Tiling layout recalculated | `Monitor*` |

## Hook Points

```c
typedef struct {
    float default_cfact;        // Default cfact for new clients
    int allow_zero;             // Allow cfact = 0 (min is normally 0.25)
} CFactConfig;

void on_client_cfact_changed(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    CFactConfig* cfg = userdata;
    
    LOG_DEBUG("Client %s cfact: %.2f -> %.2f", c->name, evt->old_val, evt->new_val);
    
    // Trigger re-tile if client is visible and tiled
    if (ISVISIBLE(c) && !c->isfloating) {
        arrange(c->mon);
    }
}
```

## Configuration

```c
static CFactConfig cfact_config = {
    .default_cfact = 1.0,
    .min_cfact = 0.25,
    .max_cfact = 4.0,
};
```

## Keybindings

```c
// Adjust cfact of focused client
{ MODKEY | ShiftMask, XK_h, setcfact, {.f = +0.05} },   // Increase
{ MODKEY | ShiftMask, XK_l, setcfact, {.f = -0.05} },   // Decrease
{ MODKEY | ShiftMask, XK_o, setcfact, {.f = 0.0} },     // Reset to 1.0
```

## Tile Algorithm with CFacts

```c
// Phase 1: Count and accumulate
n = 0; mfacts = 0; sfacts = 0;
for (c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
    n++;
    if (n <= m->nmaster)
        mfacts += c->cfact;    // Sum of master cfacts
    else
        sfacts += c->cfact;    // Sum of stack cfacts
}

// Phase 2: Resize with proportional heights
for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
    if (i < m->nmaster) {
        // Master area
        if (mfacts > 0)
            h = (m->wh - my) * (c->cfact / mfacts);  // Proportional
        else
            h = (m->wh - my) / (m->nmaster - i);      // Equal fallback
        resize(c, m->wx, m->wy + my, mw - 2*c->bw, h - 2*c->bw, 0);
        my += HEIGHT(c);
        mfacts -= c->cfact;  // Decrement for next client
    }
}
```

## Interactions

- Works with `tile()` layout — doesn't affect monocle or float
- `cfact` persists across tag switches (stored in client)
- `bstack` layout also respects cfact
- Respects minimum size hints — cfact doesn't override size constraints

---

*Extension ID: cfact*
*Priority: Medium (popular for multi-pane layouts)*
*DWM patch equivalent: dwm-cfacts*