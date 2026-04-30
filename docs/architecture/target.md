# Target Design

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-30*

> **💡 Paradigm Shift:** State machines live on targets. Not on components, not global. Each Client has its own FullscreenSM. Each Monitor has its own ConnectionSM.

---

## Purpose

A Target is an entity that exists in the system and can be the subject of state transitions. Targets own:
- Their properties (geometry, title, etc.)
- Adopted state machines (on-demand allocation)
- Adopted component listeners

Examples: CLIENT (windows), MONITOR (displays), KEYBOARD (future), TAG (virtual workspaces)

---

## Target Types

```c
enum TargetType {
    TARGET_TYPE_CLIENT,      // X11 windows
    TARGET_TYPE_MONITOR,     // Physical displays (RandR outputs)
    TARGET_TYPE_KEYBOARD,    // Input devices (future)
    TARGET_TYPE_TAG,         // Virtual workspaces
    TARGET_TYPE_SYSTEM,      // Global WM state
};
```

**Key design principle:** Each target type has different compatible components.

| Target Type | Compatible Components |
|-------------|---------------------|
| CLIENT | fullscreen, floating, urgency, focus, ... |
| MONITOR | connection, resolution, bar, ... |
| KEYBOARD | (none for v1) |
| TAG | pertag (stores per-tag state), ... |
| SYSTEM | (global event handlers) |

> **Important:** Tags are **not** properties of monitors — tags are their own targets.
> A tag (e.g., "work", "web", "9") is a virtual workspace that exists independently.
> Monitors "view" tags via the `pertag` component, which bridges TAG targets and MONITOR targets.
> See [TAG Target](#tag-target) below.

---

## Base Target Structure

```c
struct Target {
    // Identity
    TargetID id;            // unique ID (window ID, output ID, etc.)
    TargetType type;        // what kind of target
    
    // Adopted components
    Component** adopted_components;
    uint32_t num_components;
    
    // Adopted state machines
    // Stored as name → SM mapping
    StateMachine** sms;     // array of adopted SMs
    char** sm_names;         // corresponding names
    uint32_t num_sms;
    
    // Type-specific data
    void* data;             // points to Client*, Monitor*, etc.
};
```

---

## Client Target

```c
struct Client {
    // Base target
    Target target;
    
    // X properties
    xcb_window_t window;
    char* title;
    char* class_name;
    
    // Geometry
    int x, y;
    uint16_t width, height;
    uint16_t border_width;
    
    // Monitor association
    Monitor* monitor;
    
    // Tags
    uint32_t tags;          // bitmask of assigned tags
    
    // Managed state
    bool managed;           // added to window list
    bool urgent;            // has urgency hint
    bool focusable;          // can receive focus
    
    // For clientlist ordering
    Client* next;
    Client* prev;
    Client* slist_next;     // stacking list next
};
```

### Client Creation
```c
Client* client_create(xcb_window_t window) {
    Client* c = malloc(sizeof(Client));
    
    // Base target initialization
    c->target.id = window;
    c->target.type = TARGET_TYPE_CLIENT;
    c->target.data = c;
    c->target.adopted_components = NULL;
    c->target.num_components = 0;
    c->target.sms = NULL;
    c->target.num_sms = 0;
    
    // X properties
    c->window = window;
    c->title = NULL;
    c->class_name = NULL;
    c->monitor = NULL;
    c->tags = 0;
    c->managed = false;
    c->urgent = false;
    c->focusable = true;
    c->next = c->prev = NULL;
    c->slist_next = NULL;
    
    // Register with hub
    hub_register_target(&c->target);
    
    // Adopt compatible components
    Component** comps = hub_get_components_for_target_type(TARGET_TYPE_CLIENT);
    for (int i = 0; comps[i]; i++) {
        component_adopt(comps[i], &c->target);
    }
    
    return c;
}
```

---

## Monitor Target

```c
struct Monitor {
    // Base target
    Target target;
    
    // XRandR properties
    xcb_randr_output_t output;
    xcb_randr_crtc_t crtc;
    
    // Geometry
    int x, y;               // screen position
    uint16_t width, height; // resolution
    
    // Tag state — **Note:** This is a temporary simplification.
    // Tags should be their own targets (see TAG Target section below).
    // The proper design uses the `pertag` component to bridge TAG → MONITOR.
    // TODO: Remove tagset/prevtagset once TAG targets are implemented.
    uint32_t tagset;         // bitmask of visible tags
    uint32_t prevtagset;     // for switching
    
    // Layout
    Layout* current_layout;
    Layout* layouts[];       // available layouts (pertag stores per-index)
    
    // Tiling
    float mfact;             // master factor (0.0 - 1.0)
    int nmaster;             // number in master area
    
    // Client list
    Client* clients;         // first client
    Client* sel;             // selected client
    Client* stack;          // stacking order (bottom to top)
    
    // Bar
    Bar* bar;
    
    // Next monitor (for iteration)
    Monitor* next;
};
```

### Monitor Creation
```c
Monitor* monitor_create(xcb_randr_output_t output) {
    Monitor* m = malloc(sizeof(Monitor));
    
    // Base target
    m->target.id = (TargetID)output;
    m->target.type = TARGET_TYPE_MONITOR;
    m->target.data = m;
    
    // RandR properties
    m->output = output;
    m->crtc = XCB_NONE;
    
    // Default state
    m->tagset = 1 << 0;  // tag 1
    m->mfact = 0.5;
    m->nmaster = 1;
    m->current_layout = &layouts[LAYOUT_TILE];
    m->clients = NULL;
    m->sel = NULL;
    m->stack = NULL;
    m->bar = NULL;
    m->next = NULL;
    
    // Register with hub
    hub_register_target(&m->target);
    
    // Adopt compatible components
    Component** comps = hub_get_components_for_target_type(TARGET_TYPE_MONITOR);
    for (int i = 0; comps[i]; i++) {
        component_adopt(comps[i], &m->target);
    }
    
    return m;
}
```

---

## TAG Target

Tags are virtual workspaces that exist independently of monitors. A monitor "views" a subset of tags via the `pertag` component.

```c
struct Tag {
    // Base target
    Target target;
    
    // Identity
    int index;               // 0-8 (for 9 tags)
    char* name;              // optional: "work", "web", "9", etc.
    
    // Bitmask for this tag (1 << index)
    uint32_t mask;
    
    // Next tag (for iteration)
    Tag* next;
};
```

### TAG Target Design Principles

1. **Tags exist independently** — A tag like "tag 1" exists whether or not any monitor is viewing it
2. **Monitors "view" tags** — via the `pertag` component, a monitor indicates which tags it displays
3. **Client ↔ Tag relationship** — clients have a tagmask; they're visible on monitors whose tagset overlaps
4. **TAG target is simple** — tags don't adopt many components; they're mainly referenced by `pertag`

### Tag → Client Visibility

A client is visible on a monitor when:
```c
bool client_visible_on_monitor(Client* c, Monitor* m) {
    return (c->tags & m->tagset) != 0;
}
```

### Monitor ↔ Tag Relationship (via pertag)

```
MONITOR ──adopts──> pertag component ──references──> TAG targets
```

The `pertag` component bridges monitors and tags:
- Stores per-tag layout parameters (mfact, nmaster, layout) for each monitor
- Maps from tag index → TAG target
- When user switches tags, the component updates the monitor's active view

---

## Target Adoption

> ⚠️ **Implementation Note:** Early implementations incorrectly tried to hardcode SM references in targets. The correct approach uses a generic "adopted components" list with on-demand SM lookup. See "Anti-Pattern: Hardcoding SMs in Targets" in decisions.md.

When a target is created, it adopts all compatible components from the registry.

**Key insight:** Targets do NOT know about specific SMs. They only know which components they've adopted. SMs are looked up by name via `target_get_sm()`.

### Adoption Process
```c
void target_adopt(Target* t, Component* c) {
    // Check if already adopted
    for (uint32_t i = 0; i < t->num_components; i++) {
        if (t->adopted_components[i] == c) return;
    }
    
    // Add to adopted list
    t->adopted_components = realloc(t->adopted_components,
        (t->num_components + 1) * sizeof(Component*));
    t->adopted_components[t->num_components++] = c;
    
    // Call component's on_adopt hook
    c->on_adopt(t);
}
```

### On-Demand SM Allocation
```c
StateMachine* target_get_sm(Target* t, const char* sm_name) {
    // Check if SM already exists
    for (uint32_t i = 0; i < t->num_sms; i++) {
        if (strcmp(t->sm_names[i], sm_name) == 0) {
            return t->sms[i];
        }
    }
    
    // Find component that provides this SM
    Component* c = NULL;
    for (uint32_t i = 0; i < t->num_components; i++) {
        SMTemplate* tmpl = t->adopted_components[i]->get_sm_template();
        if (tmpl && strcmp(tmpl->name, sm_name) == 0) {
            c = t->adopted_components[i];
            break;
        }
    }
    
    if (!c) return NULL;  // No component provides this SM
    
    // Create SM lazily
    SMTemplate* tmpl = c->get_sm_template();
    StateMachine* sm = sm_create(t, tmpl);
    
    // Add to target's SM list
    t->sms = realloc(t->sms, (t->num_sms + 1) * sizeof(StateMachine*));
    t->sm_names = realloc(t->sm_names, (t->num_sms + 1) * sizeof(char*));
    t->sms[t->num_sms] = sm;
    t->sm_names[t->num_sms] = strdup(sm_name);
    t->num_sms++;
    
    return sm;
}
```

---

## Target Destruction

```c
void target_destroy(Target* t) {
    // Call component unadopt hooks
    for (uint32_t i = 0; i < t->num_components; i++) {
        t->adopted_components[i]->on_unadopt(t);
    }
    
    // Destroy all state machines
    for (uint32_t i = 0; i < t->num_sms; i++) {
        sm_destroy(t->sms[i]);
        free(t->sm_names[i]);
    }
    free(t->sms);
    free(t->sm_names);
    
    // Free adopted components array
    free(t->adopted_components);
    
    // Unregister from hub
    hub_unregister_target(t->id);
}
```

---

## Target Lookup

```c
// Get target by ID
Target* target_by_id(TargetID id) {
    return hub_get_target(id);
}

// Get target by X window
Client* client_by_window(xcb_window_t window) {
    Target* t = hub_get_target(window);
    return t ? (Client*)t->data : NULL;
}

// Get all clients
Client** get_all_clients(void) {
    Target** targets = hub_get_targets_by_type(TARGET_TYPE_CLIENT);
    Client** clients = malloc(sizeof(Client*) * (target_count + 1));
    for (int i = 0; targets[i]; i++) {
        clients[i] = targets[i]->data;
    }
    clients[target_count] = NULL;
    return clients;
}
```

---

## Component Filtering

Components declare which target types they accept:

```c
Component fullscreen_component = {
    .name = "fullscreen",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_CLIENT },
    .num_targets = 1,
    // ...
};

Component connection_component = {
    .name = "connection",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_MONITOR },
    .num_targets = 1,
    // ...
};
```

When a target is created, the hub finds compatible components:

```c
Component** hub_get_components_for_target_type(TargetType type) {
    static Component* results[64];
    uint32_t count = 0;
    
    for (Component* c = registry; c; c = c->next) {
        for (uint32_t i = 0; i < c->num_targets; i++) {
            if (c->accepted_targets[i] == type) {
                results[count++] = c;
                break;
            }
        }
    }
    results[count] = NULL;
    
    return results;
}
```

---

## On-Demand vs Eager Adoption

### Current Design: Eager for Built-in Components

When a target is created, it immediately adopts all compatible components. This ensures:
- All functionality is available immediately
- No race conditions on first use
- Simpler code

### Future: Plugin Adoption

For components loaded at runtime, adoption should be optional:

```c
// Option 1: Plugin specifies what to adopt by default
// In component registration
PluginConfig gap_config = {
    .default_adoption = TARGET_TYPE_MONITOR,
};

// Option 2: User config specifies
// config.def.h:
target "monitor" adopt: gap, bar, connection, ...
```

### Unadoption

Targets can unadopt components (when components are unloaded):

```c
void target_unadopt(Target* t, Component* c) {
    // Find in adopted list
    int idx = -1;
    for (uint32_t i = 0; i < t->num_components; i++) {
        if (t->adopted_components[i] == c) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) return;  // Not adopted
    
    // Remove from list
    for (uint32_t i = idx; i < t->num_components - 1; i++) {
        t->adopted_components[i] = t->adopted_components[i + 1];
    }
    t->num_components--;
    
    // Call component's on_unadopt hook
    c->on_unadopt(t);
    
    // Remove SM if it exists
    SMTemplate* tmpl = c->get_sm_template();
    if (tmpl) {
        target_remove_sm(t, tmpl->name);
    }
}
```

---

## Special Targets: Symbolic References

The hub supports symbolic targets that resolve to concrete targets:

```c
enum SpecialTargets {
    TARGET_CURRENT_CLIENT = 0,     // = 0, invalid as real ID
    TARGET_CURRENT_MONITOR = 1,
    TARGET_ALL_CLIENTS = 2,
    TARGET_ALL_MONITORS = 3,
    // ...
};

// Real targets use window IDs, output IDs, etc. which are > 100
#define IS_SYMBOLIC(target) ((target) < 100)
#define TARGET_CLIENT(id) ((TargetID)((id) + 100))
```

Resolution happens in the hub before routing:

```c
TargetID hub_resolve_target(TargetID symbolic) {
    if (symbolic == TARGET_CURRENT_CLIENT) {
        return focus_component_get_current_client();
    }
    if (symbolic == TARGET_CURRENT_MONITOR) {
        return monitor_component_get_current_monitor();
    }
    if (symbolic == TARGET_ALL_CLIENTS || IS_SYMBOLIC(symbolic)) {
        return symbolic;  // handled specially by router
    }
    return symbolic;  // already concrete
}
```

---

## Header: target.h

```c
#ifndef TARGET_H
#define TARGET_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"
#include "component.h"
#include "sm.h"

typedef uint64_t TargetID;
typedef uint32_t TargetType;

// Base target structure
typedef struct Target {
    TargetID id;
    TargetType type;
    void* data;  // points to Client*, Monitor*, etc.
    
    // Adopted components
    Component** adopted_components;
    uint32_t num_components;
    
    // Adopted state machines
    StateMachine** sms;
    char** sm_names;
    uint32_t num_sms;
} Target;

// Target lifecycle
Target* target_create(TargetType type, void* data);
void target_destroy(Target* t);

// Component adoption
void target_adopt(Target* t, Component* c);
void target_unadopt(Target* t, Component* c);

// State machine lookup
StateMachine* target_get_sm(Target* t, const char* sm_name);
void target_remove_sm(Target* t, const char* sm_name);

// Target types
enum TargetType {
    TARGET_TYPE_CLIENT,
    TARGET_TYPE_MONITOR,
    TARGET_TYPE_KEYBOARD,
    TARGET_TYPE_TAG,
    TARGET_TYPE_SYSTEM,
};

// Special targets
enum SpecialTargets {
    TARGET_CURRENT_CLIENT = 0,
    TARGET_CURRENT_MONITOR = 1,
    TARGET_ALL_CLIENTS = 2,
    TARGET_ALL_MONITORS = 3,
};

#define IS_SYMBOLIC(target) ((target) < 100)

#endif // TARGET_H
```

---

## Type-Specific Headers

```c
// client.h
#ifndef CLIENT_H
#define CLIENT_H

#include "target.h"

struct Client {
    // Base
    Target target;
    
    // X
    xcb_window_t window;
    char* title;
    char* class_name;
    
    // Geometry
    int x, y;
    uint16_t width, height;
    uint16_t border_width;
    
    // Relations
    Monitor* monitor;
    uint32_t tags;
    
    // State
    bool managed;
    bool urgent;
    bool focusable;
    
    // List
    Client* next;
    Client* prev;
    Client* slist_next;
};

Client* client_create(xcb_window_t window);
void client_destroy(Client* c);

// Helper: get SM
#define client_get_sm(c, name) target_get_sm(&(c)->target, name)

#endif // CLIENT_H

// monitor.h
#ifndef MONITOR_H
#define MONITOR_H

#include "target.h"
#include "layout.h"

struct Monitor {
    // Base
    Target target;
    
    // RandR
    xcb_randr_output_t output;
    xcb_randr_crtc_t crtc;
    
    // Geometry
    int x, y;
    uint16_t width, height;
    
    // Tag state
    uint32_t tagset;
    uint32_t prevtagset;
    
    // Tiling
    float mfact;
    int nmaster;
    Layout* current_layout;
    
    // Clients
    Client* clients;
    Client* sel;
    Client* stack;
    
    // Bar
    Bar* bar;
    
    // Next
    Monitor* next;
};

Monitor* monitor_create(xcb_randr_output_t output);
void monitor_destroy(Monitor* m);

#define monitor_get_sm(m, name) target_get_sm(&(m)->target, name)

#endif // MONITOR_H
```

---

## Testing Strategy

1. **Unit tests for target creation**: Verify components adopted
2. **Unit tests for on-demand SM allocation**: First access creates SM
3. **Unit tests for component adoption/unadoption**: Correct hooks called
4. **Unit tests for target lookup**: By ID, by window, by type
5. **Integration tests**: Full lifecycle (create → use → destroy)

---

*See also:*
- `architecture/overview.md` — High-level architecture
- `architecture/component.md` — Component design
- `architecture/state-machine.md` — SM framework
- `architecture/hub.md` — Hub routing
