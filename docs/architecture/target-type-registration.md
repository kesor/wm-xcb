# Dynamic Target Type Registration

*Part of architecture documentation — Design Proposal*
*Issue: #99*
*Supersedes: #91*

---

## Problem

Currently, target types are hardcoded in `wm-hub.h`:

```c
enum {
  TARGET_TYPE_CLIENT,
  TARGET_TYPE_MONITOR,
  TARGET_TYPE_KEYBOARD,
  TARGET_TYPE_TAG,
  TARGET_TYPE_SYSTEM,
  TARGET_TYPE_COUNT,
};
```

This violates separation of concerns — target types should be introduced by the components that own them, not centralized in the hub.

## Design Goals

1. **Decoupling**: Components don't need to coordinate on target type IDs
2. **Extensibility**: New components can introduce new target types without modifying hub
3. **Ownership**: Each component owns its target types, matching component-based architecture
4. **Performance**: Fast integer-based lookup for common operations

## Proposed Solution

### Core Concept: Target Type Registry

Components register their target types with the hub at initialization. The hub maintains a registry of target types indexed by name (for extensibility) and by integer (for fast lookup).

```c
/*
 * Target type definition
 * Introduced by a component, registered with the hub.
 */
typedef struct TargetType {
  const char*     name;       // e.g., "client", "monitor", "focused-client"
  uint32_t        id;         // unique integer ID (assigned on registration)
  HubComponent*   owner;      // component that introduced this type
  
  /* Optional: resolver for symbolic targets of this type */
  TargetID (*resolve)(const char* symbolic);
} TargetType;
```

### Component Declares Its Target Types

Components declare what target types they introduce:

```c
typedef struct HubComponent {
  const char*      name;
  
  /* Target types this component INTRODUCES (it owns these) */
  TargetType*      introduced_targets;
  uint32_t         num_introduced;
  
  /* Target types this component ACCEPTS (it works with these) */
  TargetType**     accepted_targets;  // by name
  uint32_t         num_accepted;
  
  // ... rest unchanged
} HubComponent;
```

### Example: Focus Component

```c
// Focus component introduces TARGET_TYPE_FOCUSED_CLIENT
static TargetType focus_introduced_types[] = {
  {
    .name    = "focused-client",
    .resolve = focus_resolve_current_client,
  },
};

// Focus component accepts TARGET_TYPE_CLIENT (any managed client)
static TargetType* focus_accepted_types[] = {
  hub_get_target_type_by_name("client"),  // resolved at init
  NULL,
};

HubComponent focus_component = {
  .name               = "focus",
  .introduced_targets  = focus_introduced_types,
  .num_introduced      = 1,
  .accepted_targets    = focus_accepted_types,
  .num_accepted        = 1,
  // ...
};
```

### Example: Client List Component

```c
// Client list introduces TARGET_TYPE_CLIENT (managed X11 windows)
static TargetType client_list_introduced_types[] = {
  {
    .name    = "client",
    .resolve = NULL,  // concrete targets, no symbolic resolution
  },
};

static TargetType* client_list_accepted_types[] = {
  hub_get_target_type_by_name("client"),
  NULL,
};

HubComponent client_list_component = {
  .name               = "client-list",
  .introduced_targets  = client_list_introduced_types,
  .num_introduced      = 1,
  .accepted_targets    = client_list_accepted_types,
  .num_accepted        = 1,
  // ...
};
```

### Target Type Registration

When a component registers with the hub, it also registers its target types:

```c
void hub_register_component(HubComponent* comp) {
  // ... existing component registration ...
  
  // Register target types introduced by this component
  for (uint32_t i = 0; i < comp->num_introduced; i++) {
    TargetType* tt = &comp->introduced_targets[i];
    tt->id    = hub_register_target_type(tt->name, comp);
  }
}
```

### Target Type Lookup

Two lookup mechanisms:
1. **By name**: For extensibility and configuration
2. **By ID**: For fast runtime lookup

```c
/* By name - for configuration and component reference */
TargetType* hub_get_target_type_by_name(const char* name);

/* By ID - for fast runtime lookup in hot paths */
TargetType* hub_get_target_type_by_id(uint32_t id);

/* Get all target types */
TargetType** hub_get_all_target_types(uint32_t* count);
```

### Hub Target Structure

Targets now reference target types by pointer (or ID for fast lookup):

```c
struct HubTarget {
  TargetID       id;         // concrete ID (window, output, etc.)
  TargetType*    type;       // reference to registered type (fast path)
  // or: uint32_t   type_id;   // ID for fast lookup
  bool           registered;
};
```

### Backward Compatibility

For backward compatibility, we provide a transition layer:

```c
// Legacy names map to new target types
#define TARGET_TYPE_CLIENT    (hub_get_target_type_by_name("client")->id)
#define TARGET_TYPE_MONITOR   (hub_get_target_type_by_name("monitor")->id)
#define TARGET_TYPE_TAG       (hub_get_target_type_by_name("tag")->id)
// etc.
```

This way existing code using `TARGET_TYPE_CLIENT` continues to work.

### Built-in Target Types

The first components to initialize register the base target types:

| Component | Introduces | Notes |
|-----------|-----------|-------|
| client-list | `client` | Managed X11 windows |
| monitor-manager | `monitor` | RandR outputs/displays |
| tag-manager | `tag` | Virtual workspaces |

These are "built-in" in the sense that they're registered early, but they're still introduced by components, not hardcoded in the hub.

## Symbolic Target Resolution

Components can provide resolvers for their target types:

```c
typedef TargetID (*TargetResolver)(const char* symbolic);

TargetID focus_resolve_current_client(const char* symbolic) {
  if (strcmp(symbolic, "current") == 0) {
    return focus_component.focused_window;
  }
  return TARGET_ID_NONE;
}
```

The hub uses these resolvers to resolve symbolic targets:

```c
TargetID hub_resolve_symbolic(const char* target_name, const char* symbolic) {
  TargetType* tt = hub_get_target_type_by_name(target_name);
  if (tt && tt->resolve) {
    return tt->resolve(symbolic);
  }
  return TARGET_ID_NONE;
}
```

## Implementation Phases

### Phase 1: Target Type Registry (Issue #99)
- [x] Create TargetType structure
- [x] Add hub_register_target_type() function
- [x] Add hub_get_target_type_by_name() function
- [x] Add hub_get_target_type_by_id() function
- [x] Update component registration to register target types
- [x] Provide backward compatibility macros

### Phase 2: Component Updates (Issue #91)
- [x] Update client-list component to declare `client` target type
- [x] Update monitor-manager component to declare `monitor` target type
- [x] Update tag-manager component to declare `tag` target type
- [x] Update all components to use new target type declaration

### Phase 3: Symbolic Resolution (Future)
- [ ] Add resolver field to TargetType
- [ ] Implement hub_resolve_symbolic()
- [ ] Update focus component with resolver
- [ ] Update hub request routing for symbolic targets

## Open Questions

1. **Should target types be strings or integers for the API?**
   - Current proposal: Strings for configuration/API, integers internally for performance
   - Alternative: Pure strings (simpler, but slower)

2. **How to handle duplicate target type names?**
   - Proposal: Second component to register a type with same name fails
   - Alternative: Allow override (last wins)

3. **Should target types be unregisterable?**
   - Proposal: No (once registered, they persist for the session)
   - Rationale: Simplifies lookup, targets already have the type

## Related Issues

- **#91**: Components Should Introduce Their Own Target Types (blocked by this design)
- **#83**: Actions and Wiring Architecture (uses target resolution)
- **#84**: Configuration System for Keybindings, Rules, and Properties

## Decision Log

| Decision | Rationale |
|----------|-----------|
| Components introduce target types | Separation of concerns - hub shouldn't know about specific types |
| String-based names + integer IDs | Balances extensibility (strings) with performance (integers) |
| Target types registered at component init | Single place for lifecycle management |
| Resolver per target type | Allows components to define their own symbolic target semantics |

---

*See also:*
- `architecture/hub.md` — Hub implementation
- `architecture/target.md` — Target design
- `architecture/decisions.md` — Decision rationale