# Extension: Scratchpads

## Overview

Manages "scratchpad" windows — hidden windows that can be quickly shown/hidden with a key binding. Each scratchpad is identified by a unique key, and toggling shows/hides all scratchpads with that key. Multiple scratchpads per key are supported.

## DWM Reference

Based on `togglescratch()`, `showhide.md`, `manage.md`:
- `scratchkey` field in `Client` struct (char, 0 = not a scratchpad)
- `spawnscratch()` launches program with specific key
- `togglescratch()` iterates all clients, shows/hides matching scratchpads
- Scratchpads shown: `c->tags = mon->tagset[seltags]` (current view tags)
- Scratchpads hidden: `c->tags = 0` (all tags cleared)
- `scratchvisible` count tracks how many are currently visible
- `numscratchpads` tracks total scratchpads per key
- Multi-monitor: scratchpads moved to current monitor if all on same monitor

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_SCRATCHPAD_SHOWN` | Scratchpad shown | `Client*`, `key`, `Monitor*` |
| `EVT_SCRATCHPAD_HIDDEN` | Scratchpad hidden | `Client*`, `key` |
| `EVT_SCRATCHPAD_SPAWNED` | New scratchpad spawned | `key`, `Monitor*` |
| `EVT_SCRATCHPAD_TOGGLE` | Toggle requested | `key`, `Monitor*` |

## Hook Points

```c
typedef struct {
    char key;                    // Scratchpad identifier
    const char** cmd;           // Command to spawn (argv-style)
    int visible_count;           // How many are currently visible
    int total_count;            // Total scratchpads with this key
    int last_monitor;          // Monitor where scratchpad last lived
} ScratchpadConfig;

typedef struct {
    char key;
    const char** cmd;
    int floating;               // Scratchpad is floating
    const char* position;       // Floatpos rule (optional)
    int tags;                  // Tags to show on (0 = current view)
} ScratchpadRule;

static ScratchpadRule scratchpads[] = {
    { .key = 's', .cmd = (const char*[]){"ghostty", "--instance-name=scratch", NULL} },
    { .key = 'c', .cmd = (const char*[]){"speedcrunch", NULL} },
    { .key = 'v', .cmd = (const char*[]){"nvim", "-c", "VimwikiMake打卡", NULL} },
    { .key = 0 }  // End
};

void on_scratchpad_toggle(const Event* evt, void* userdata) {
    ScratchpadContext* ctx = userdata;
    char key = ctx->key;
    
    // Count visible and total scratchpads
    int visible = 0, total = 0;
    for (Client* c = all_clients; c; c = c->next) {
        if (c->scratchkey != key) continue;
        total++;
        if (c->tags & current_tagset) visible++;
    }
    
    if (visible == 0 && total == 0) {
        // Spawn new scratchpad
        spawn_scratchpad(key);
        Event spawn_evt = { .type = EVT_SCRATCHPAD_SPAWNED, .data = &key };
        bus_emit(&spawn_evt);
    } else if (visible == total) {
        // Hide all
        for (Client* c = all_clients; c; c = c->next) {
            if (c->scratchkey == key) {
                c->tags = 0;
                Event hide_evt = { .type = EVT_SCRATCHPAD_HIDDEN, .subject = c };
                bus_emit(&hide_evt);
            }
        }
    } else {
        // Show all
        for (Client* c = all_clients; c; c = c->next) {
            if (c->scratchkey == key) {
                c->tags = current_tagset;
                Event show_evt = { .type = EVT_SCRATCHPAD_SHOWN, .subject = c };
                bus_emit(&show_evt);
            }
        }
    }
    
    arrange(NULL);  // Update visibility
}
```

## Configuration

```c
static ScratchpadConfig scratchpad_config = {
    .center_on_show = 1,      // Center floating scratchpads when shown
    .follow_monitor = 1,      // Move scratchpad to current monitor on show
    .auto_hide = 1,            // Auto-hide when moving to different tag
};
```

## Keybindings

```c
static Key keys[] = {
    { MODKEY, XK_grave, togglescratch, {.v = scratchtermcmd} },
    { MODKEY | ControlMask, XK_grave, togglescratch, {.v = scratchcalcmd} },
};
```

## Scratchpad Rule Format

```c
// First element (char*) is the key, rest is command
static const char* scratchtermcmd[] = { "s", "ghostty", "--instance-name=scratch", NULL };
static const char* scratchcalcmd[] = { "c", "speedcrunch", NULL };
```

## Multi-Scratchpad Logic

From `togglescratch()` in dwm:
1. First pass: count visible/total, track which monitor they're on
2. If all on same monitor ≠ current → move to current monitor
3. If all visible → hide (clear tags)
4. If all hidden → show (set to current tagset)
5. If partially visible → hide all

## Interactions

- Works with `floatpos` — scratchpads can have position rules
- Works with `pertag` — scratchpad visibility is per-tag via `c->tags`
- `showhide()` handles visibility (moves off-screen when hidden)
- Auto-hide: when tag changes, scratchpads with `tags != 0` get `tags = 0`

---

*Extension ID: scratchpad*
*Priority: High (very popular feature)*
*DWM patch equivalent: dwm-scratchpads*