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

## State Machine Events

| Event | Direction | Description |
|-------|-----------|-------------|
| `EVT_SCRATCHPAD_SHOWN` | Emits | Scratchpad shown |
| `EVT_SCRATCHPAD_HIDDEN` | Emits | Scratchpad hidden |
| `EVT_SCRATCHPAD_SPAWNED` | Emits | New scratchpad spawned |
| `EVT_TAG_VIEW_CHANGED` | Subscribes | Tag changed (may hide scratchpads) |

## Component Design

**Target type:** `TARGET_TYPE_CLIENT`

**Provides:** ScratchpadSM (per-client, tracks scratchkey and visibility)

**Subscribes to:** `EVT_TAG_VIEW_CHANGED`

**Emits:** `EVT_SCRATCHPAD_SHOWN`, `EVT_SCRATCHPAD_HIDDEN`, `EVT_SCRATCHPAD_SPAWNED`

```c
Component scratchpad_component = {
    .name = "scratchpad",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_CLIENT },
    .requests = (RequestType[]){ REQ_SCRATCHPAD_TOGGLE, REQ_SCRATCHPAD_SPAWN },
    .events = (EventType[]){ 
        EVT_SCRATCHPAD_SHOWN,
        EVT_SCRATCHPAD_HIDDEN,
        EVT_SCRATCHPAD_SPAWNED
    },
    .on_init = scratchpad_on_init,
    .executor = scratchpad_executor,
    .get_sm_template = scratchpad_get_template,
};
```

## Hooks

```c
void scratchpad_on_init(void) {
    hub_subscribe(EVT_TAG_VIEW_CHANGED, on_tag_view_changed, NULL);
}

void on_tag_view_changed(const Event* evt, void* userdata) {
    // Auto-hide scratchpads when leaving a tag
    for (Client* c = all_clients; c; c = c->next) {
        if (c->scratchkey != 0 && !(c->tags & evt->new_tagset)) {
            c->tags = 0;
            hub_emit(EVT_SCRATCHPAD_HIDDEN, c->target.id, NULL);
        }
    }
}
```

## Configuration

```c
typedef struct {
    int center_on_show;     // Center floating scratchpads when shown
    int follow_monitor;      // Move scratchpad to current monitor on show
    int auto_hide;          // Auto-hide when moving to different tag
} ScratchpadConfig;

typedef struct {
    char key;                    // Scratchpad identifier
    const char** cmd;           // Command to spawn (argv-style)
    int floating;               // Scratchpad is floating
    const char* position;       // Floatpos rule (optional)
    int tags;                  // Tags to show on (0 = current view)
} ScratchpadRule;

static ScratchpadConfig scratchpad_config = {
    .center_on_show = 1,      // Center floating scratchpads when shown
    .follow_monitor = 1,      // Move scratchpad to current monitor on show
    .auto_hide = 1,            // Auto-hide when moving to different tag
};

static ScratchpadRule scratchpads[] = {
    { .key = 's', .cmd = (const char*[]){"ghostty", "--instance-name=scratch", NULL} },
    { .key = 'c', .cmd = (const char*[]){"speedcrunch", NULL} },
    { .key = 'v', .cmd = (const char*[]){"nvim", "-c", "VimwikiMake打卡", NULL} },
    { .key = 0 }  // End
};
```

## Keybindings

```c
static Key keys[] = {
    { MODKEY, XK_grave, togglescratch, {.v = scratchtermcmd} },
    { MODKEY | ControlMask, XK_grave, togglescratch, {.v = scratchcalcmd} },
};
```

## Interactions with Other Components

| Component | Interaction |
|-----------|-------------|
| **Pertag** | Scratchpad visibility is per-tag via `c->tags` |
| **Floatpos** | Scratchpads can have position rules |
| **Tiling** | Scratchpads are excluded from tiling when visible |
| **Focus** | When shown, scratchpad gains focus |
| **Bar** | May be excluded from client list |

---

*Extension ID: scratchpad*
*Priority: High (very popular feature)*
*DWM patch equivalent: dwm-scratchpads*
*Hub: Registers with Hub, subscribes to EVT_TAG_VIEW_CHANGED, emits scratchpad events*
