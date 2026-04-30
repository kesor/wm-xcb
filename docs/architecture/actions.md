# Actions and Wiring Architecture

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-30*

> **💡 Design Decision:** Actions are the public API of components. Keybindings don't hardcode actions — they query the action registry and wire triggers to action names.

---

## Purpose

The Actions and Wiring system provides:
1. **Components expose callable actions** — e.g., `fullscreen.toggle()`, `tag_manager.view(1)`
2. **Declarative wiring** — config wires key combos to action names
3. **Target resolution** — actions receive targets at call time, not binding time

This decouples keybindings from component internals. A component owns its actions; config wires triggers to them.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        CONFIG LAYER                              │
│                                                                  │
│  config.def.h:                                                   │
│    keybinding_register("Mod+f",  "fullscreen.toggle");           │
│    keybinding_register("Mod+1",  "tag-manager.view", 1);         │
│    keybinding_register("Mod+Enter", "focus.focus-current");      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        ACTION REGISTRY                           │
│                                                                  │
│  Component A:           Component B:           Component C:     │
│    "fullscreen.toggle"    "tag-manager.view"     "focus.focus"   │
│    "fullscreen.enable"    "tag-manager.toggle"                     │
│                                                                  │
│  Each action knows:                                              │
│  - Name (unique string)                                           │
│  - Signature (parameters)                                         │
│  - Handler function                                               │
│  - Default target resolver                                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        KEYBINDING COMPONENT                      │
│                                                                  │
│  KEY_PRESS → lookup(key, mods) → resolve target → invoke action  │
│                                                                  │
│  KeyBindingMap:                                                   │
│    "Mod+f" → ActionRef { name="fullscreen.toggle", target=NONE } │
└─────────────────────────────────────────────────────────────────┘
```

---

## Action Definition

An **Action** is a callable function with a name and signature.

### Action Structure

```c
/*
 * Action callback signature
 * Returns true on success, false on failure.
 */
typedef bool (*ActionCallback)(ActionInvocation* inv);

/*
 * Action invocation context
 * Passed to action callbacks with resolved target and arguments.
 */
typedef struct ActionInvocation {
  TargetID target;           /* Resolved target (e.g., current client) */
  void*    data;             /* Action-specific data (e.g., tag number) */
  uint64_t correlation_id;   /* For async response tracking */
  void*    userdata;         /* Per-binding userdata */
} ActionInvocation;

/*
 * Action definition
 */
typedef struct Action {
  const char*       name;        /* Unique action name, e.g., "fullscreen.toggle" */
  const char*       description; /* Human-readable description */
  ActionCallback    callback;    /* Function to call */
  TargetResolver    target_resolver; /* How to resolve .target field */
  void*             userdata;    /* Component-specific data */
} Action;

/*
 * Target resolver function
 * Called at action invocation to determine the target.
 * Return TARGET_ID_NONE if no target available.
 */
typedef TargetID (*TargetResolver)(ActionInvocation* inv);

/*
 * Built-in target resolvers
 */
TargetID resolve_current_client(ActionInvocation* inv);   /* Focus component */
TargetID resolve_current_monitor(ActionInvocation* inv); /* Monitor manager */
TargetID resolve_bound_target(ActionInvocation* inv);    /* From binding */
TargetID resolve_none(ActionInvocation* inv);             /* No target needed */
```

### Action Registration API

```c
/*
 * Register an action with the action registry.
 * Returns true on success, false if name already exists.
 */
bool action_register(Action* action);

/*
 * Unregister an action by name.
 * Returns true if found and removed, false if not found.
 */
bool action_unregister(const char* name);

/*
 * Look up an action by name.
 * Returns NULL if not found.
 */
Action* action_lookup(const char* name);

/*
 * Get all registered actions.
 * Returns NULL-terminated array.
 */
Action** action_get_all(void);
```

---

## Binding Definition

A **Binding** connects a trigger (key combination) to an action (named function).

### Binding Structure

```c
/*
 * Key binding trigger types
 */
typedef enum {
  BINDING_TYPE_KEY,      /* Keyboard key combination */
  BINDING_TYPE_BUTTON,   /* Mouse button */
  BINDING_TYPE_POINTER,  /* Pointer drag/resize */
} BindingType;

/*
 * Binding entry
 */
typedef struct Binding {
  BindingType   type;
  
  /* For key bindings */
  uint32_t     modifiers;
  xcb_keycode_t keycode;
  
  /* For button bindings */
  xcb_button_t button;
  
  /* For pointer bindings */
  /* ... pointer-specific fields ... */
  
  /* The action to invoke */
  const char*  action_name;  /* e.g., "fullscreen.toggle" */
  
  /* Optional binding-specific data passed to action */
  void*        userdata;
} Binding;

/*
 * Binding lookup result
 */
typedef struct BindingResult {
  const char*  action_name;  /* Action name, e.g., "fullscreen.toggle" */
  void*        userdata;     /* Binding-specific data */
} BindingResult;
```

---

## Keybinding Component Changes

The keybinding component is refactored to use the action registry:

### Old Model (Anti-pattern)

```c
// ❌ WRONG - Hardcoded in keybinding component
static const KeyBinding default_keybindings[] = {
  { XCB_MOD_MASK_4, 41, KEYBINDING_ACTION_TOGGLE_FULLSCREEN, 0 },
};

void execute_keybinding(const KeyBinding* binding) {
  switch (binding->action) {
  case KEYBINDING_ACTION_TOGGLE_FULLSCREEN:
    hub_send_request(REQ_CLIENT_FULLSCREEN, TARGET_CURRENT_CLIENT);
    break;
  // Hardcoded knowledge of other components!
  }
}
```

### New Model (Correct)

```c
// ✅ CORRECT - Config wires keys to actions
static const KeyBinding default_bindings[] = {
  { XCB_MOD_MASK_4, 41, "fullscreen.toggle" },  /* keycode 41 = 'f' */
  { XCB_MOD_MASK_4, 36, "focus.focus-current" },
};

// Keybinding component just looks up and invokes actions
void execute_keybinding(const KeyBinding* binding) {
  Action* action = action_lookup(binding->action_name);
  if (!action) {
    LOG_WARN("No action found: %s", binding->action_name);
    return;
  }
  
  ActionInvocation inv = {
    .target = action->target_resolver ? 
              action->target_resolver() : 
              binding->bound_target,
    .data   = binding->userdata,
  };
  
  action->callback(&inv);
}
```

### Keybinding Component API

```c
/*
 * Register a keybinding that invokes an action.
 * 
 * @param modifiers  XCB modifier mask (e.g., XCB_MOD_MASK_4)
 * @param keycode    X11 keycode (e.g., 41 for 'f')
 * @param action     Action name to invoke (e.g., "fullscreen.toggle")
 * @param data       Optional data passed to action callback
 * @return           Binding ID or 0 on failure
 */
uint32_t keybinding_register(uint32_t modifiers, xcb_keycode_t keycode,
                             const char* action, void* data);

/*
 * Register a keybinding with integer argument (e.g., tag numbers).
 */
uint32_t keybinding_register_with_arg(uint32_t modifiers, xcb_keycode_t keycode,
                                      const char* action, uint32_t arg);

/*
 * Unregister a keybinding by ID.
 */
void keybinding_unregister(uint32_t binding_id);

/*
 * Lookup binding by modifiers and keycode.
 * Returns NULL if not found.
 */
const KeyBinding* keybinding_lookup(uint32_t modifiers, xcb_keycode_t keycode);

/*
 * Get all registered bindings.
 */
const KeyBinding* keybinding_get_all(void);
uint32_t keybinding_get_count(void);
```

---

## Target Resolution

### Symbolic Targets

Actions can declare which target type they need:

```c
/*
 * Target type hints for actions
 */
typedef enum {
  TARGET_TYPE_CURRENT_CLIENT,   /* Resolved by focus component */
  TARGET_TYPE_CURRENT_MONITOR,  /* Resolved by monitor manager */
  TARGET_TYPE_ANY,              /* No specific requirement */
} ActionTargetType;

/*
 * Action with target type hint
 */
typedef struct Action {
  const char*         name;
  const char*         description;
  ActionCallback      callback;
  ActionTargetType   target_type;     /* What target this action needs */
  bool               target_required; /* Fail if no target available */
  void*              userdata;
} Action;
```

### Resolution at Call Time

```c
void invoke_action(const char* action_name, ActionBinding* binding) {
  Action* action = action_lookup(action_name);
  if (!action) return;
  
  /* Resolve target based on action's target_type */
  TargetID target = TARGET_ID_NONE;
  
  if (action->target_type == TARGET_TYPE_CURRENT_CLIENT) {
    target = focus_get_current_client();  /* From focus component */
  } else if (action->target_type == TARGET_TYPE_CURRENT_MONITOR) {
    target = monitor_get_current_monitor();  /* From monitor manager */
  } else {
    target = binding->target;  /* Use bound target if specified */
  }
  
  /* Check if target is required */
  if (action->target_required && target == TARGET_ID_NONE) {
    LOG_DEBUG("Action '%s' requires target but none available", action_name);
    return;
  }
  
  /* Invoke with resolved target */
  ActionInvocation inv = {
    .target = target,
    .data   = binding->data,
  };
  
  action->callback(&inv);
}
```

---

## Component Action Registration

Components register their actions during `on_init()`:

### Fullscreen Component

```c
/*
 * Fullscreen action: toggle fullscreen
 */
static bool
fullscreen_action_toggle(ActionInvocation* inv)
{
  if (inv->target == TARGET_ID_NONE) {
    LOG_DEBUG("fullscreen.toggle: no target");
    return false;
  }
  
  Client* c = hub_get_target(inv->target);
  if (!c) return false;
  
  StateMachine* sm = fullscreen_get_sm(c);
  FullscreenState new_state = (fullscreen_get_state(c) == FULLSCREEN_STATE_FULLSCREEN)
    ? FULLSCREEN_STATE_WINDOWED
    : FULLSCREEN_STATE_FULLSCREEN;
  
  return sm_transition(sm, new_state);
}

Action fullscreen_actions[] = {
  {
    .name        = "fullscreen.toggle",
    .description = "Toggle fullscreen on current client",
    .callback    = fullscreen_action_toggle,
    .target_type = TARGET_TYPE_CURRENT_CLIENT,
    .target_required = true,
  },
};

void
fullscreen_init(void)
{
  /* Register actions */
  for (int i = 0; i < sizeof(fullscreen_actions)/sizeof(fullscreen_actions[0]); i++) {
    action_register(&fullscreen_actions[i]);
  }
}
```

### Tag Manager Component

```c
/*
 * Tag view action: show specific tag
 */
static bool
tag_action_view(ActionInvocation* inv)
{
  if (inv->target == TARGET_ID_NONE) {
    return false;
  }
  
  Monitor* m = hub_get_target(inv->target);
  if (!m) return false;
  
  uint32_t tag = *(uint32_t*) inv->data;
  tag_manager_view(m, tag);
  return true;
}

Action tag_manager_actions[] = {
  {
    .name        = "tag-manager.view",
    .description = "View tag N (1-9)",
    .callback    = tag_action_view,
    .target_type = TARGET_TYPE_CURRENT_MONITOR,
    .target_required = true,
  },
  {
    .name        = "tag-manager.toggle",
    .description = "Toggle tag N visibility",
    .callback    = tag_action_toggle,
    .target_type = TARGET_TYPE_CURRENT_MONITOR,
    .target_required = true,
  },
  {
    .name        = "tag-manager.client-move",
    .description = "Move focused client to tag N",
    .callback    = tag_action_client_move,
    .target_type = TARGET_TYPE_CURRENT_CLIENT,
    .target_required = true,
  },
};

void
tag_manager_init(void)
{
  for (int i = 0; i < sizeof(tag_manager_actions)/sizeof(tag_manager_actions[0]); i++) {
    action_register(&tag_manager_actions[i]);
  }
}
```

---

## Configuration Layer

### config.def.h Style

```c
/*
 * config.def.h - Default keybindings
 *
 * This file wires key combinations to component actions.
 * Components own their actions; this layer just wires triggers to them.
 */

/*
 * Helper macros for common patterns
 */

/* Basic keybinding: Mod+<key> → action */
#define KEY(mod, keycode, action) \
  keybinding_register(mod, keycode, action, NULL)

/* Keybinding with argument: Mod+Shift+<num> → action(arg) */
#define TAG_KEY(mod, keycode, action, tag) \
  keybinding_register_with_arg(mod, keycode, action, tag)

/*
 * Focus actions
 */
KEY(XCB_MOD_MASK_4, 36, "focus.focus-current");           /* Mod+Enter */
KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 36, "focus.focus-prev");  /* Mod+Shift+Enter */

/*
 * Fullscreen action
 */
KEY(XCB_MOD_MASK_4, 41, "fullscreen.toggle");              /* Mod+f (keycode 41 = f) */

/*
 * Tag view actions: Mod+1 through Mod+9
 */
TAG_KEY(XCB_MOD_MASK_4, 10, "tag-manager.view", 1);         /* Mod+1 */
TAG_KEY(XCB_MOD_MASK_4, 11, "tag-manager.view", 2);        /* Mod+2 */
TAG_KEY(XCB_MOD_MASK_4, 12, "tag-manager.view", 3);       /* Mod+3 */
TAG_KEY(XCB_MOD_MASK_4, 13, "tag-manager.view", 4);       /* Mod+4 */
TAG_KEY(XCB_MOD_MASK_4, 14, "tag-manager.view", 5);        /* Mod+5 */
TAG_KEY(XCB_MOD_MASK_4, 15, "tag-manager.view", 6);        /* Mod+6 */
TAG_KEY(XCB_MOD_MASK_4, 16, "tag-manager.view", 7);        /* Mod+7 */
TAG_KEY(XCB_MOD_MASK_4, 17, "tag-manager.view", 8);        /* Mod+8 */
TAG_KEY(XCB_MOD_MASK_4, 18, "tag-manager.view", 9);       /* Mod+9 */

/*
 * Tag toggle actions: Mod+Shift+1 through Mod+Shift+9
 */
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 10, "tag-manager.toggle", 1);   /* Mod+Shift+1 */
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 11, "tag-manager.toggle", 2);  /* ... */
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 12, "tag-manager.toggle", 3);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 13, "tag-manager.toggle", 4);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 14, "tag-manager.toggle", 5);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 15, "tag-manager.toggle", 6);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 16, "tag-manager.toggle", 7);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 17, "tag-manager.toggle", 8);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_4, 18, "tag-manager.toggle", 9);

/*
 * Client-tag toggle: Mod+Shift+Alt+1 through Mod+Shift+Alt+9
 */
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 10, "tag-manager.client-move", 1);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 11, "tag-manager.client-move", 2);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 12, "tag-manager.client-move", 3);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 13, "tag-manager.client-move", 4);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 14, "tag-manager.client-move", 5);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 15, "tag-manager.client-move", 6);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 16, "tag-manager.client-move", 7);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 17, "tag-manager.client-move", 8);
TAG_KEY(XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_5 | XCB_MOD_MASK_4, 18, "tag-manager.client-move", 9);

/*
 * Monitor tiling
 */
KEY(XCB_MOD_MASK_4, 31, "monitor.tile");                  /* Mod+i */
```

---

## Cross-Component Wiring (Pointer)

The action system also enables pointer bindings:

```c
/*
 * Pointer binding: button drag → action
 * Used for floating window resize, moving, etc.
 */
typedef struct PointerBinding {
  uint32_t    modifiers;    /* Modifiers that must be held */
  xcb_button_t button;      /* Mouse button */
  const char* action;       /* Action to invoke on drag */
  const char* end_action;   /* Action to invoke on release */
} PointerBinding;

/*
 * Floating component registers pointer actions
 */
Action floating_actions[] = {
  {
    .name = "floating.move-start",
    .callback = floating_move_start,
  },
  {
    .name = "floating.move-end", 
    .callback = floating_move_end,
  },
  {
    .name = "floating.resize-start",
    .callback = floating_resize_start,
  },
  {
    .name = "floating.resize-end",
    .callback = floating_resize_end,
  },
};

/*
 * Config wires pointer bindings
 */
void config_pointer_init(void) {
  /* Mod+Left click: move floating window */
  pointer_binding_register(0, XCB_BUTTON_INDEX_1, 
                           "floating.move-start", "floating.move-end");
  
  /* Mod+Right click: resize floating window */
  pointer_binding_register(0, XCB_BUTTON_INDEX_3,
                           "floating.resize-start", "floating.resize-end");
}
```

---

## Implementation Files

| File | Purpose |
|------|---------|
| `src/actions/action-registry.c/h` | Action registration and lookup |
| `src/actions/action-invoke.c/h` | Action invocation with target resolution |
| `src/components/keybinding.c` | Refactored to use action registry |
| `src/components/keybinding-binding.c/h` | Binding storage and lookup |
| `config.def.h` | Wiring configuration |

---

## Testing Strategy

1. **Action registration tests**: Register actions, verify lookup works
2. **Action invocation tests**: Invoke actions, verify callbacks called
3. **Target resolution tests**: Mock resolvers, verify correct target
4. **Keybinding lookup tests**: Press key, verify correct action invoked
5. **Integration tests**: Full flow keypress → action → effect

---

## Migration Path

### Phase 1: Action Registry (This Implementation)
- Add `action_register()` / `action_lookup()` API
- Components register actions in `on_init()`
- Keybinding component looks up actions by name

### Phase 2: Binding Refactor
- Remove hardcoded `KeyBinding.action` enum
- Replace with `action_name` string
- Update `config.def.h` to use action names

### Phase 3: Runtime Config
- Add JSON/YAML config file support
- Replace compile-time `config.def.h` with runtime config
- Add `action_unregister()` for hot-reload

---

*See also:*
- `architecture/component.md` — Component design
- `architecture/decisions.md` — Decision log
- `architecture/hub.md` — Hub routing