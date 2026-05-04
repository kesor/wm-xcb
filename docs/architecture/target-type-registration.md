# Dynamic Target Type Registration

*Part of architecture documentation — Design Proposal*
*Issue: #99*
*Supersedes: #91*

---

## Problem

Previously, target types were hardcoded in `wm-hub.h`:

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

This violated separation of concerns — target types should be introduced by the components that own them, not centralized in the hub.

## Design Goals

1. **Decoupling**: Components don't need to coordinate on target type IDs
2. **Extensibility**: New components can introduce new target types without modifying hub
3. **Ownership**: Each component owns its target types, matching component-based architecture
4. **Performance**: Fast integer-based lookup for common operations

## Implemented Solution

### HubTargetType Structure

Target types are now dynamically registered structures:

```c
struct HubTargetType {
  const char*   name;     /* e.g., "client", "monitor", "focused-client" */
  uint32_t      id;       /* unique integer ID, assigned on registration */
  HubComponent* owner;    /* component that introduced this type */
  bool          reserved; /* true once registered */
};
```

### String-Based Target Type Declaration

Components declare accepted target types via string names, which are resolved at registration time:

```c
struct HubComponent {
  const char*      name;           /* component identifier */
  
  /* Target types this component ACCEPTS (works with these)
   * NULL-terminated array of target type names.
   * Resolved to HubTargetType* at component registration time.
   */
  const char**     accepted_target_names;
  
  /* Resolved target types (populated at registration) */
  HubTargetType**  accepted_targets;
  
  // ... rest unchanged
} HubComponent;
```

### Example: Focus Component

```c
// Focus component accepts "client" target type
static const char* client_targets[] = { "client", NULL };

HubComponent focus_component = {
  .name                  = "focus",
  .accepted_target_names = client_targets,
  .accepted_targets      = NULL,  /* populated at hub_register_component() */
  // ...
};
```

### Example: Client List Component

```c
// Client list accepts "client" target type
static const char* client_targets[] = { "client", NULL };

HubComponent client_list_component = {
  .name                  = "client-list",
  .accepted_target_names = client_targets,
  .accepted_targets      = NULL,
  // ...
};
```

### Target Type Registration

Built-in types are registered at `hub_init()`:

```c
void hub_init(void) {
  // ...
  (void) hub_register_target_type("client", NULL);
  (void) hub_register_target_type("monitor", NULL);
  (void) hub_register_target_type("tag", NULL);
}
```

Components can register additional types at any time:

```c
HubTargetType* hub_register_target_type(const char* name, HubComponent* owner);
```

When a component registers, its `accepted_target_names` are resolved to `HubTargetType*` pointers and stored in `accepted_targets`:

```c
void hub_register_component(HubComponent* comp) {
  // ...
  /* Resolve accepted target type names to pointers */
  uint32_t accepted_count = 0;
  if (comp->accepted_target_names != NULL) {
    for (uint32_t i = 0; comp->accepted_target_names[i] != NULL; i++) {
      accepted_count++;
    }
  }
  
  /* Allocate and populate accepted_targets array */
  if (accepted_count > 0) {
    comp->accepted_targets = calloc(accepted_count + 1, sizeof(HubTargetType*));
    // ... resolve each name and store pointer
  }
}
```

### Target Type Lookup

Two lookup mechanisms:
1. **By name**: For extensibility and configuration
2. **By ID**: For fast runtime lookup

```c
/* By name - returns NULL if not found */
HubTargetType* hub_get_target_type_by_name(const char* name);

/* By ID - returns NULL if not found or invalid ID */
HubTargetType* hub_get_target_type_by_id(uint32_t id);

/* Get integer ID by name - returns TARGET_TYPE_INVALID if not found */
uint32_t hub_get_target_type_id_by_name(const char* name);

/* Get all registered target types */
HubTargetType** hub_get_all_target_types(uint32_t* count);

/* Get components accepting a type by name */
HubComponent** hub_get_components_for_target_type_name(const char* type_name);
```

### HubTarget Structure

Targets reference target types by ID:

```c
struct HubTarget {
  TargetID     id;
  TargetTypeId type_id;  /* ID for fast lookup, maps to HubTargetType */
  bool         registered;
};
```

### Legacy Constants

For backward compatibility, enum constants are provided but deprecated:

```c
/* Deprecated - use hub_get_target_type_id_by_name("client") instead */
enum {
  TARGET_TYPE_CLIENT   = 0,
  TARGET_TYPE_MONITOR  = 1,
  TARGET_TYPE_TAG      = 2,
  TARGET_TYPE_COUNT    = 3,
};

/* Legacy: for terminating target arrays */
#define TARGET_TYPE_NONE UINT32_MAX
```

The values match the registration order in `hub_init()` and are resolved at runtime via `resolve_target_type_id()`.

### Unknown Type Handling

Unknown or unregistered type IDs return `TARGET_TYPE_INVALID` instead of silently mapping to another type:

```c
static uint32_t resolve_target_type_id(uint32_t legacy_id) {
  if (target_type_count > 0 && legacy_id < target_type_count &&
      target_types[legacy_id].reserved) {
    return legacy_id;
  }
  return TARGET_TYPE_INVALID;  /* Explicit error, not silent fallback */
}
```

### Built-in Target Types

The hub registers base target types at initialization:

| Type | ID | Introduced By |
|------|----|---------------|
| client | 0 | hub_init() (placeholder for client-list) |
| monitor | 1 | hub_init() (placeholder for monitor-manager) |
| tag | 2 | hub_init() (placeholder for tag-manager) |

These are registered early to ensure consistent IDs, but future extensions can register additional types.

## Implementation Status

### Phase 1: Target Type Registry (Issue #99)
- [x] Create HubTargetType structure
- [x] Add hub_register_target_type() function
- [x] Add hub_get_target_type_by_name() function
- [x] Add hub_get_target_type_id_by_name() function
- [x] Add hub_get_target_type_by_id() function
- [x] Add hub_get_all_target_types() function
- [x] Populate accepted_targets at registration

### Phase 2: Component Updates (Issue #91)
- [x] Update client-list component to declare `accepted_target_names`
- [x] Update monitor-manager component to declare `accepted_target_names`
- [x] Update tag-manager component to declare `accepted_target_names`
- [x] Update focus component to use `accepted_target_names`
- [x] Update fullscreen component to use `accepted_target_names`
- [x] Update keybinding component to use `accepted_target_names`
- [x] Update pertag component to use `accepted_target_names`
- [x] Update tiling component to use `accepted_target_names`

### Phase 3: Target Creation Updates
- [x] Update client.c to use hub_get_target_type_id_by_name("client")
- [x] Update monitor.c to use hub_get_target_type_id_by_name("monitor")
- [x] Update tag.c to use hub_get_target_type_id_by_name("tag")

### Phase 4: Testing
- [x] Add target type registry tests to test-wm-hub.c

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| `accepted_target_names` instead of `introduced_targets` | Simpler API - components declare what they accept, not what they introduce |
| `accepted_targets` populated at registration | No null-pointer issues during component initialization |
| `TARGET_TYPE_INVALID` for unknown types | Explicit error handling instead of silent fallback |
| Built-in types registered in hub_init() | Ensures consistent IDs across all components |

## Related Issues

- **#91**: Components Should Introduce Their Own Target Types
- **#83**: Actions and Wiring Architecture (uses target resolution)
- **#84**: Configuration System for Keybindings, Rules, and Properties

## See Also

- `architecture/hub.md` — Hub implementation
- `architecture/target.md` — Target design
- `architecture/decisions.md` — Decision rationale
