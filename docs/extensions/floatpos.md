# Extension: Floatpos (Floating Window Position Rules)

## Overview

Allows floating windows to be positioned according to configurable rules. Each rule specifies a position and size for matching windows (by class, instance, or title). Rules are applied on window manage and can be toggled per-client.

## DWM Reference

Based on `togglefloating.md`, `configurerequest.md`, `manage.md`:
- `floatpos` function parses position strings like "50% 50% 50% 50%"
- `setfloatpos()` applies the position to a client
- `getfloatpos()` does the actual geometry computation
- Position format: "x y w h" where each can be:
  - `NN%` — percentage of monitor
  - `NNp` — pixel offset
  - `G` — grid-based (with floatposgrid_x/y)
  - `A` — absolute
  - `C` — center
  - `S` — sticky
  - `Z` — right-aligned

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_CLIENT_MANAGED` | New client registered | `Client*`, `Monitor*` |
| `EVT_CLIENT_FLOAT_ENABLED` | Client became floating | `Client*` |
| `EVT_CLIENT_FLOAT_DISABLED` | Client became tiled | `Client*` |
| `EVT_CLIENT_FLOAT_MOVED` | Floating client moved | `Client*`, `x`, `y`, `w`, `h` |
| `EVT_RULE_APPLIED` | Rule matched and applied | `Client*`, `Rule*` |

## Position Format

```
position := x y w h
x := percent | pixel | grid | special
y := percent | pixel | grid | special
w := percent | pixel | grid | special
h := percent | pixel | grid | special

percent := NUMBER '%'     // e.g., "50%"
pixel := NUMBER 'p'        // e.g., "100p" (pixels from edge)
grid := NUMBER 'G'        // e.g., "3G" (grid position)
special:
  | 'A'                   // absolute
  | 'C'                   // center
  | 'S'                   // sticky (relative to client)
  | 'Z'                   // right-aligned
  | 'M'                   // mouse position
```

## Example Rules

```c
static FloatPosRule rules[] = {
    // Steam: full screen
    { .class = "Steam", .position = "0 0 100% 100%" },
    
    // Dialogs: center, 50% size
    { .class = NULL, .title = "Save As", .position = "50% 50% 50% 50%" },
    
    // File picker: right side, 60% width, top half
    { .instance = "filechooser", .position = "40% 0 60% 50%" },
    
    // Terminal scratchpad: grid-based
    { .scratchkey = 's', .position = "6G 6G 1 1" },
    
    { .class = NULL }  // End
};
```

## Hook Points

```c
typedef struct {
    const char* class;
    const char* instance;
    const char* title;
    char scratchkey;         // 0 = not a scratchpad
    const char* position;    // Position string
} FloatPosRule;

void on_client_managed(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    
    // Find matching rule
    FloatPosRule* rule = find_matching_rule(c);
    if (rule && rule->position) {
        // Apply position
        apply_floatpos(c, rule->position);
        
        // Emit event for other components to react
        Event rule_evt = {
            .type = EVT_RULE_APPLIED,
            .subject = c,
            .data = rule
        };
        bus_emit(&rule_evt);
    }
}

void on_client_float_enabled(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    FloatPosConfig* cfg = userdata;
    
    // Re-apply position if rule exists
    FloatPosRule* rule = find_matching_rule(c);
    if (rule && rule->position && cfg->reapply_on_toggle) {
        apply_floatpos(c, rule->position);
    }
}
```

## Configuration

```c
typedef struct {
    FloatPosRule* rules;           // Array of rules
    int grid_x;                    // Grid columns (default: 12)
    int grid_y;                   // Grid rows (default: 12)
    int reapply_on_toggle;        // Re-apply position when toggling float
    int reapply_on_monitor_change; // Re-apply when moving between monitors
} FloatPosConfig;
```

## Keybindings

```c
static Key keys[] = {
    // Apply floatpos to focused client (use last rule or default)
    { MODKEY | ShiftMask, XK_f, floatpos_apply, {.v = "50% 50% 50% 50%"} },
    
    // Cycle through preset positions
    { MODKEY | ControlMask, XK_f, floatpos_cycle, {0} },
};
```

## Interactions

- Applied on `manage()` for new windows with rules
- Re-applied when `togglefloating()` enables floating
- Not applied to tiled clients (position ignored for tiled)
- Works with `pertag` — position stored per-client, not per-tag
- `floatposgrid_x/y` stored in config

---

*Extension ID: floatpos*
*Priority: High (commonly requested feature)*
*DWM patch equivalent: dwm-floatpos*