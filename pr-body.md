## Summary

Implements the component adoption pattern for the Hub, as described in the architecture documentation.

## Changes

### Hub Changes (`wm-hub.c/h`)

1. **Added adoption hooks to HubComponent**:
   - `on_adopt` - called when a target adopts this component
   - `on_unadopt` - called when a target unadopts this component

2. **Added `hub_get_components_for_target_type()`**:
   - Returns all registered components that accept a given target type
   - Returns a NULL-terminated array

3. **Added `hub_adopt_components_for_target()`**:
   - Calls `on_adopt` hook for all compatible components when a target is registered

4. **Modified `hub_register_target()`**:
   - Now automatically calls `hub_adopt_components_for_target()` after registering
   - Targets adopt all compatible components automatically on creation

### Pertag Component (`src/components/pertag.c/h`)

1. **Made pertag a proper HubComponent**:
   - Now registers with hub declaring it accepts `TARGET_TYPE_MONITOR`
   - Provides `pertag_component_init()` and `pertag_component_shutdown()` functions
   - Uses existing `pertag_on_adopt()` and `pertag_on_unadopt()` hooks

2. **Updated `wm.c`**:
   - Initializes pertag component at startup
   - Shuts down pertag component at shutdown

### Tests (`test-wm-hub.c`)

Added new test group `HubComponentAdoption` with tests:
- `test_adoption_hooks_called_on_target_register()` - verifies on_adopt is called
- `test_adoption_only_for_compatible_targets()` - verifies type filtering works
- `test_adoption_for_multiple_targets_of_same_type()` - verifies each target gets adoption
- `test_get_components_for_target_type()` - verifies component lookup by target type

## Architecture Alignment

This implements the adoption pattern described in `docs/architecture/`:
- **Target.md** - Target Adoption section
- **Component.md** - Component interface with lifecycle hooks
- **Hub.md** - Registry and target resolution

## Related Issues

- Issue #70 (removed hard-coupled pertag from Monitor)
- Issue #73 (this implementation)

## Testing

All 598 tests pass, including the 16 new adoption tests.
