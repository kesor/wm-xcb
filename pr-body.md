# Remove mock stubs from Client target

## Summary

Refactored the Client target's state machine storage from hardcoded fields to a dynamic, extensible array-based storage. This removes the mock stubs pattern where Client had explicit fields for each state machine type.

## Changes

### Architecture

**Before:** Client had hardcoded `sms.fullscreen`, `sms.floating`, `sms.urgency`, `sms.focus` fields
**After:** Client has dynamic `sms.sms[]` (array of pointers) and `sms.names[]` (corresponding names)

### Files Modified

1. **src/target/client.h**
   - Changed `sms` struct from fixed fields to dynamic storage:
     ```c
     struct {
       StateMachine** machines; /* array of SM pointers */
       char**         names;    /* corresponding SM names */
       uint32_t       count;    /* number of SMs */
       uint32_t       capacity; /* allocated capacity */
     } sms;
     ```

2. **src/target/client.c**
   - Added `client_sm_find()` helper to look up SM by name
   - Added `client_sm_set_internal()` helper to add/update SMs
   - Updated `client_destroy()` to iterate and clean up dynamic arrays
   - Updated `client_get_sm()` and `client_set_sm()` to use the new helpers

3. **src/components/focus.c**
   - Updated all `c->sms.focus` accesses to use `client_get_sm(c, "focus")`
   - Updated all `c->sms.focus = sm` to use `client_set_sm(c, "focus", sm)`

4. **src/components/fullscreen.c**
   - Updated all `c->sms.fullscreen` accesses to use `client_get_sm(c, "fullscreen")`
   - Updated all `c->sms.fullscreen = sm` to use `client_set_sm(c, "fullscreen", sm)`

5. **test-focus-component.c**
   - Updated test assertions to use `client_get_sm(c, "focus")` instead of `c->sms.focus`

## Acceptance Criteria

- ✅ No mock stubs in Client target - state machines are stored by name, not by type
- ✅ Client only contains core target functionality - the storage is generic
- ✅ Features are implemented as separate components - focus and fullscreen components now store their SMs by name

## Benefits

1. **Extensibility**: New state machines can be added without modifying Client structure
2. **Decoupling**: Client no longer has knowledge of specific feature types (focus, fullscreen, etc.)
3. **Clean API**: Components use `client_get_sm()` and `client_set_sm()` with string names
4. **Proper memory management**: SM names are properly freed on client destruction