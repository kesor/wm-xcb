# Project Status

*Audit run: 2026-11-01 at the state of `master` right after PR #112 merged.*
*Everything below was checked by running the code, not by reading the docs.*

> **Bottom line:** the framework is built and unit-tested. The window manager
> runs, talks to the X server, discovers the monitor, and tracks client windows.
> It does not map, position, tile, focus, or show the windows it tracks, and no
> keybinding can fire. The next milestone is "first tiled window on screen";
> everything else waits behind it.

---

## 1. What works today

| Area | State | Evidence |
|------|-------|----------|
| Build (`make`) | works | gcc, `-Os`, links against libxcb, xcb-randr, xcb-ewmh, xcb-xinput, xcb-keysyms |
| Unit tests (`make test`) | 645 assertions pass, 0 fail | 18 test files at repo root |
| Seam tests (`make test-x`) | **2 assertions pass, 9 fail** | `tests/x/`, 8 files; every failure maps to a defect in §4 |
| SM standalone tests (`make test-sm-standalone`) | pass | `All SM tests passed!` |
| Hub standalone tests (`make test-standalone`) | **link error** | `undefined reference to 'logger'`: `wm-log.o` missing from the link line |
| Debug build (`make DEBUG=1`) | works (fixed 2026-11-01) | was broken by a zero-length array in `src/components/tiling.c`; now `NULL` with count 0 |
| Start under Xephyr | works | takes substructure redirect, stays alive, exits on SIGTERM |
| RandR monitor discovery | works | log: `Monitor manager: Creating monitor for output 571`, geometry `1280x800+0+0` |
| Target adoption | partial | monitor adopts `pertag`; monitors created at startup never adopt `tiling` (see §4.11) |
| Client tracking | partial | `CREATE_NOTIFY` creates a `Client` target and registers it with the hub; `MAP_REQUEST` sets the managed flag |
| Window display | **does not work** | after creating 2 windows: 0 viewable, both remain unmapped |
| Keybindings | **cannot fire** | 32 bindings registered, zero key grabs issued |
| Actions | 2 of 9 registered | `app.launch` (dmenu, PR #110) and `terminal.spawn` (PR #112); the other seven names in `config.wm.def.h` resolve to nothing |
| EWMH | atoms only | no root properties set, so no external tool or panel can detect the WM |
| Idle CPU | **100% of one core** | measured 1990 ms CPU over 2000 ms wall in `scripts/perf-test.sh` |

Code size: 10,916 lines in `src/`, 3,042 lines in the root-level `wm-*.c` / `config*.c`
files, 7,729 lines of tests.

---

## 2. What the WM is supposed to become

The goal has not changed since the design work in `docs/VISION.md` and
`docs/IMPLEMENTATION-ROADMAP.md`:

- Replace dwm's patch culture with components that subscribe to events and send
  requests through a Hub, so two extensions that touch the same state coexist
  without patching each other.
- State machines own state. They live on targets (`Client`, `Monitor`, `Tag`),
  components only supply templates.
- Raw writes for reality (X events), guarded transitions for intentions
  (keybindings, plugins).
- Same user-visible workflow as dwm: `MOD+1..9` tags, `MOD+Enter` terminal,
  `MOD+Shift+c` close, plus `MOD+d` launcher and extension keys.

Read `docs/VISION.md` for the reasoning, `docs/architecture/overview.md` for the
concepts, and `docs/architecture/decisions.md` for why each choice was made.

---

## 3. What exists in the tree

### Core

| File | Role |
|------|------|
| `wm.c` | entry point: init order, event loop, shutdown order |
| `wm-hub.c/.h` | registry (components, target types, targets), request router, event bus |
| `src/sm/*` | state machine framework: templates, instances, guards, actions, hooks, registry |
| `src/target/*` | `Client`, `Monitor`, `Tag` targets, each owning its state machines |
| `src/xcb/xcb-handler.c` | XCB event handler registry; components register handlers at init |
| `wm-xcb.c` | connection, root window, substructure redirect, event loop, startup rescan |
| `wm-xcb-events.c` | legacy per-event handling kept from the pre-hub code |
| `wm-xcb-ewmh.c` | EWMH atom interning, `_NET_WM_STATE_FULLSCREEN` query |
| `config.c` / `config.def.h` / `config.wm.def.h` | configuration layer and keybinding wiring |
| `src/actions/*` | action registry, keybinding bindings, dmenu launcher |

### Components (`src/components/`)

| Component | Target | Requests handled | XCB events owned | Emits |
|-----------|--------|------------------|------------------|-------|
| `client-list` | client | — | CREATE_NOTIFY, DESTROY_NOTIFY, MAP_REQUEST, UNMAP_NOTIFY | `EVT_CLIENT_CREATED/DESTROYED/MANAGED/UNMANAGED` |
| `focus` | client | — | ENTER_NOTIFY, FOCUS_IN | `EVT_CLIENT_FOCUSED/UNFOCUSED` |
| `fullscreen` | client | `REQ_CLIENT_FULLSCREEN` (10) | PROPERTY_NOTIFY | `EVT_FULLSCREEN_ENTERED/EXITED/FAILED` |
| `keybinding` | — | (sends, never handles) | KEY_PRESS, KEY_RELEASE | — |
| `monitor-manager` | monitor | — | RANDR_NOTIFY | `EVT_MONITOR_CONNECTED/DISCONNECTED` |
| `tiling` | monitor | `REQ_MONITOR_TILE` (20) | — | `EVT_LAYOUT_CHANGED` |
| `tag-manager` | monitor, client | `REQ_TAG_VIEW/TOGGLE/CLIENT_TOGGLE` (7,8,9) | — | `EVT_TAG_CHANGED` |
| `pertag` | monitor | — | — | — |
| `connection-sm` | monitor | — | — | (SM template only) |

### Documented but not implemented

- `floating`, `urgency`, `pointer` (mouse move/resize), `bar` components.
- All 11 extension designs in `docs/extensions/` except `pertag`.

---

## 4. Defects and gaps found by this audit

Ordered by what blocks the most. Each line names the file so the fix has a
starting point.

### Blockers for showing a window

1. **Windows are never mapped.** `client_list_on_map_request()`
   (`src/components/client-list.c:259`) sets the managed flag and returns; it
   does not call `xcb_map_window`. The function says so in a comment at line 283.
   Because the WM holds substructure redirect, nobody else maps the window
   either, so a mapped client stays invisible.
2. **Tiling is unreachable.** `tiling_executor()` handles `REQ_MONITOR_TILE`
   (`src/components/tiling.c:602`), but no code in the tree sends that request:
   `hub_send_request` has zero production callers. The `monitor.tile` keybinding
   in `config.wm.def.h` names an action no component registers, so the binding
   fails at `action_lookup`.
3. **No managed client ever reaches a monitor or a tag.** `client_create()`
   (`src/target/client.c:273`) does not assign a monitor or tags, so even a
   direct call to `tiling_tile_monitor()` would find no clients for the monitor.
4. **Focus is never applied to X.** No `xcb_set_input_focus` call exists
   anywhere. The focus component tracks `FocusSM` and emits events; the X server
   keeps whatever focus the client set for itself.
5. **No key grabs.** No `xcb_grab_key` call exists anywhere, so `KEY_PRESS`
   never arrives at the WM for unfocused windows. The keybinding component
   handles events it will never receive.
6. **Seven of nine configured actions are missing.** Registered:
   `app.launch` (`src/actions/launcher.c:85`) and `terminal.spawn`
   (`src/actions/terminal.c:136`). Missing: `focus.focus-current`,
   `focus.focus-prev`, `tag-manager.view`, `tag-manager.toggle`,
   `tag-manager.client-move`, `fullscreen.toggle`, `monitor.tile`.
   (`config.wm.def.h` wires all nine.)

### Correctness bugs

7. **The event loop busy-waits.** `main()` loops on `handle_xcb_events()`, which
   calls `xcb_poll_for_event()` (`wm-xcb.c:212`) and returns immediately when
   there is nothing to read. Measured cost: 100% of one core while idle.
   `xcb_wait_for_event()` or polling the file descriptor with `poll(2)` fixes it.
8. **Event type collision.** `EVT_TAG_CHANGED = 20` (`src/components/tag-manager.h:55`)
   equals `EVT_FULLSCREEN_ENTERED = 20` (`src/components/fullscreen.h:57`). A
   fullscreen subscriber today also receives tag changes.
9. **Event types above the hub limit are dropped silently.**
   `EVT_MONITOR_CONNECTED = 101` and `EVT_MONITOR_DISCONNECTED = 100`
   (`src/components/connection-sm.h:31`) exceed `MAX_EVENT_TYPES` (64);
   `hub_emit()` returns without emitting and without logging.
10. **Client lifecycle events start at 0.** `enum ClientListEventType`
    (`src/components/client-list.h:47`) assigns 0..3 to
    `EVT_CLIENT_CREATED..UNMANAGED`. Nothing collides today, but the hub has no
    per-component event namespace, so any component that starts its own enum at
    0 will collide with these.
11. **Monitors created at startup never adopt `tiling`.** `wm.c` initializes
    `monitor_manager_init()` before `tiling_component_init()`, and adoption
    happens when the monitor target is created. `pertag` is initialized first
    for exactly this reason (see the comment in `wm.c`), `tiling` is not.
12. **Startup rescan bypasses the client model.** `wm_manage_all_clients()`
    (`wm-xcb.c:151`) reparents and maps every existing top-level window but does
    not create `Client` targets for them, so pre-existing windows are visible and
    untracked.

### Repo hygiene

13. **`hub_shutdown()` leaks registry memory.** `clear_registry_state()`
    (`wm-hub.c:88`) zeroes `target_by_id_map` without freeing the hash chain
    (24 bytes per registered target), and nothing frees the `accepted_targets`
    array allocated in `hub_register_component()` (16 bytes per component). ASan
    on `./test`: 936 bytes in 47 allocations.
14. `make test-standalone` does not link: `wm-log.o` is missing from the recipe
    (`Makefile:153`).
15. `docs/IMPLEMENTATION-ROADMAP.md` claims "~75% Complete" and marks Phase 3
    components as the remaining work. Given items 1-6, that number describes the
    framework, not the window manager. `docs/README.md` is stale the same way
    ("What's Implemented" lists only the pre-hub code).
16. `README.md` (6,142 lines) is a concatenation of older design docs and is not
    an entry point for anyone. Treat `docs/` as the source of truth.

---

## 5. Where the project stands against the plan

| Plan phase | Claimed | Actual |
|------------|---------|--------|
| 1. Hub foundation | complete | complete, unit-tested |
| 2. Basic functionality | complete | data model only; nothing reaches the screen |
| 3. Built-in components | partial | fullscreen exists but is unreachable without key grabs; floating, urgency, pointer, bar absent |
| 4. Design work (actions, config) | complete | action registry and config exist; components do not register their actions |
| 5. Extensions | documented | `pertag` only |

The realistic order is: make one window appear tiled (§4.1-4.3), then make keys
work (§4.5-4.6), then focus (§4.4), then the rest.

---

## 6. Reproducing this audit

```bash
# 1. build and run the unit tests
make && make test

# 2. does the WM do what X users would see? (seam tests, expect failures)
make tools/xclient
make test-x

# 3. nested X session by hand
scripts/xephyr-test.sh --windows 2 --verbose

# 4. cost of the WM while idle and while windows appear
scripts/perf-test.sh --windows 10

# 5. what the WM actually saw (needs a debug build)
make clean && make DEBUG=1
Xephyr :2 -screen 1280x800 -noreset &
DISPLAY=:2 ./wm 2>&1 | tee /tmp/wm.log &
DISPLAY=:2 tools/xclient -n 2 -v
grep -E "MAP_REQUEST|Client created|Tiling" /tmp/wm.log
```

`docs/TESTING.md` explains each of these in more detail, including how to look
at the nested screen, where the scripts write their logs (`$TMPDIR`), and how to
add instrumentation.
