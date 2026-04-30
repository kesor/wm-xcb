## Summary

Implement tag-view component with TagViewSM state machine for managing tag visibility on monitors. This implements the core functionality for the tag/workspaces feature as specified in the PRD.

## Changes

### New Components

**Tag Manager Component** (`src/components/tag-manager.c`, `src/components/tag-manager.h`)
- TagViewSM template: tracks visible tag bitmask (EMPTY ↔ TAGGED states)
- Request handlers: `REQ_TAG_VIEW`, `REQ_TAG_TOGGLE`, `REQ_TAG_CLIENT_TOGGLE`
- Executor updates TagViewSM on tag changes
- On tag change: clients show/hide based on tag membership
- Emits `EVT_TAG_CHANGED` event on any tag state change

### Modified Components

**Keybinding Component** (`src/components/keybinding.c`, `src/components/keybinding.h`)
- Added `KEYBINDING_ACTION_TAG_CLIENT_TOGGLE` action for moving focused client to tag
- Added Mod+Shift+Alt+1-9 keybindings for client-to-tag assignment
- Routes `REQ_TAG_CLIENT_TOGGLE` to the tag manager

## Architecture

```
Keybinding (Mod+1-9) → REQ_TAG_VIEW → Hub → TagManager Executor
                                           ↓
                                       TagViewSM (monitor)
                                           ↓
                                       EVT_TAG_CHANGED
                                           ↓
                                   Client visibility updated
```

```
Keybinding (Mod+Shift+Alt+1-9) → REQ_TAG_CLIENT_TOGGLE → Hub → TagManager Executor
                                                                        ↓
                                                                   Client tags updated
                                                                        ↓
                                                                   EVT_TAG_CHANGED
```

## Acceptance Criteria Met

- ✅ Mod+1-9 shows specific tag (via `REQ_TAG_VIEW`)
- ✅ Mod+Shift+1-9 toggles tag visibility (via `REQ_TAG_TOGGLE`)  
- ✅ Mod+Shift+Alt+1-9 moves client to tag (via `REQ_TAG_CLIENT_TOGGLE`)
- ✅ Clients on hidden tags are not visible (visibility check via `tag_manager_is_client_visible()`)
- ✅ Event is emitted on tag change (via `EVT_TAG_CHANGED`)

## Testing

The tag manager component includes:
- Init/shutdown tests
- Request handling tests (tag view, toggle, client tag toggle)
- Event emission tests
- Client visibility update tests
- Multi-monitor tests
- Invalid tag index handling tests

Run `make test` to verify all existing tests pass.

## Dependencies

This PR depends on:
- Hub registry (already implemented)
- Monitor target (already implemented)
- Client target (already implemented)
- SM framework (already implemented)

All blocking issues from the original issue have been resolved.

## Related

- Parent PRD: #1
- Original Issue: #18