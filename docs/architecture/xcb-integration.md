# XCB Integration

*Part of architecture documentation — Authoritative*
*Last updated: 2026-04-30*

---

## Purpose

XCB (X C Bindings) is the X server communication layer. This document explains how raw X events flow into the component-based architecture, and how the event-driven model meets the X server.

**Key principle:** Raw XCB events go directly to component handlers. Components decide what to do with them — either send a request to the Hub, or raw-write to a state machine.

---

## The XCB → Architecture Bridge

```
┌─────────────────────────────────────────────────────────────────────┐
│                          X Server                                   │
│   - Sends XCB events for window changes, input, etc.                │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Raw XCB events
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       Event Loop                                    │
│   poll(2) on the X fd, the signal wakeup pipe, and any descriptor   │
│   a module registered; then drain:                                  │
│   xcb_poll_for_event() → xcb_generic_event_t*                       │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Dispatch
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    XCB Handler Registry                             │
│   Components register handlers at init via xcb_handler_register()   │
│   Multiple handlers can be registered for the same event type       │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Per-handler dispatch
                              ▼
┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐
│  Keybinding Comp   │  │   Focus Comp       │  │  Monitor-Manager   │
│                    │  │                    │  │       Comp         │
│ KEY_PRESS handler  │  │ ENTER_NOTIFY       │  │ RANDR handler      │
│        │           │  │        │           │  │        │           │
│        ▼           │  │        ▼           │  │        ▼           │
│ hub_send_request() │  │ sm_raw_write()     │  │ sm_raw_write()     │
└────────────────────┘  └────────────────────┘  └────────────────────┘
         │                      │                      │
         ▼                      ▼                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│                              HUB                                    │
│   - Requests go to routers → component executors                    │
│   - Raw writes cause SM transitions → events emitted                │
└─────────────────────────────────────────────────────────────────────┘
```

---

## XCB Handler Registry

Components register their own XCB event handlers. The registry (`src/xcb/xcb-handler.c/h`) manages the mapping.

### Registration API

```c
#include "xcb-handler.h"

// Register a handler for an XCB event type
int xcb_handler_register(
    XCBEventType event_type,    // e.g., XCB_KEY_PRESS
    HubComponent* component,     // component owning this handler
    void (*handler)(void* event)
);

// Example: keybinding component registers at init
void keybinding_on_init(void) {
    xcb_handler_register(XCB_KEY_PRESS, &keybinding_component, keybinding_handler);
    xcb_handler_register(XCB_KEY_RELEASE, &keybinding_component, keybinding_handler);
}
```

### Handler Structure

```c
typedef struct XCBHandler {
    XCBEventType  event_type;
    HubComponent* component;
    void (*handler)(void* event);
    struct XCBHandler* next;  // chain for multiple handlers
} XCBHandler;
```

---

## Handler → Component Decision

Once a handler receives an event, it decides:

1. **Send a request to the Hub** (for user intent)
2. **Raw-write to a state machine** (for authoritative reality changes)
3. **Both** (for some events)

### Decision Tree

```
X Event arrives
       │
       ▼
┌───────────────────┐
│ Is this user      │
│ intent? (key/btn) │
└───────────────────┘
       │
   ┌───┴───┐
   │ YES   │ NO
   ▼       ▼
┌───────┐ ┌──────────────────────┐
│Send   │ │Raw-write to SM       │
│request│ │(reality is authority)│
│to Hub │ └──────────────────────┘
└───────┘
```

### Example: Key Press

```c
void keybinding_handler(void* event) {
    xcb_key_press_event_t* e = (xcb_key_press_event_t*)event;
    
    // Look up keybinding in config
    KeyBinding* kb = lookup_keybinding(e->detail, get_modifiers(e));
    if (!kb) return;
    
    // User intent → send request to Hub
    hub_send_request(kb->request_type, kb->target);
}
```

### Example: Monitor Disconnected

```c
void monitor_manager_randr_handler(void* event) {
    xcb_randr_output_change_t* e = (xcb_randr_output_change_t*)event;
    
    if (e->connection == XCB_RANDR_CONNECTION_DISCONNECTED) {
        Monitor* m = monitor_by_output(e->output);
        if (!m) return;
        
        // Reality changed → raw-write to state machine
        StateMachine* sm = monitor_get_sm(m, "connection");
        sm_raw_write(sm, MON_STATE_DISCONNECTED);
    }
}
```

---

## Event Type Mapping

| XCB Event | Component | Action |
|-----------|-----------|--------|
| `XCB_KEY_PRESS` | keybinding | `hub_send_request()` |
| `XCB_KEY_RELEASE` | keybinding | (usually ignored) |
| `XCB_BUTTON_PRESS` | pointer | `hub_send_request()` |
| `XCB_BUTTON_RELEASE` | pointer | `hub_send_request()` |
| `XCB_MOTION_NOTIFY` | pointer | `hub_send_request()` |
| `XCB_ENTER_NOTIFY` | focus | `sm_raw_write()` to FocusSM |
| `XCB_FOCUS_IN` | focus | `sm_raw_write()` |
| `XCB_FOCUS_OUT` | focus | `sm_raw_write()` |
| `XCB_PROPERTY_NOTIFY` | fullscreen, urgency | Check atoms, maybe `sm_raw_write()` |
| `XCB_CREATE_NOTIFY` | client-list | Create client target |
| `XCB_DESTROY_NOTIFY` | client-list | Destroy client target |
| `XCB_MAP_REQUEST` | client-list | Manage client |
| `XCB_UNMAP_NOTIFY` | client-list | Unmanage client, unless the WM asked for the unmap itself |
| `XCB_CONFIGURE_REQUEST` | floating, tiling | May `hub_send_request()` |
| `XCB_EXPOSE` | bar | `hub_emit(EVT_BAR_DRAW)` |
| `XCB_RANDR_NOTIFY` | monitor-manager | `sm_raw_write()` to ConnectionSM |

---

## XCB Handler Infrastructure

### Files

- `src/xcb/xcb-handler.h` — Public API
- `src/xcb/xcb-handler.c` — Implementation
- `wm-xcb.c` — Event loop integration

### Public API

```c
#ifndef _WM_XCB_HANDLER_H_
#define _WM_XCB_HANDLER_H_

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

typedef struct HubComponent HubComponent;
typedef uint8_t XCBEventType;

int xcb_handler_register(XCBEventType event_type, HubComponent* component, void (*handler)(void*));
void xcb_handler_unregister_component(HubComponent* component);
void xcb_handler_dispatch(void* event);
void xcb_handler_init(void);
void xcb_handler_shutdown(void);

#endif // _WM_XCB_HANDLER_H_
```

### Event Loop Integration

The loop blocks, it does not spin. `wm_xcb_wait_for_events()` waits in `poll(2)`
on the X connection's file descriptor, on the read end of the signal wakeup
pipe, and on every descriptor a module registered with
`wm_loop_add_fd_handler()`; `handle_xcb_events()` then dispatches everything
that arrived, runs those descriptors' handlers, and flushes once. A connection
that has already failed ends the loop: `poll(2)` on that socket returns at once,
which is the busy-wait again. With no wakeup pipe, the wait uses a 250 ms
timeout so a signal is still noticed.

An event source is how a module waits on a child without waiting. The launcher
forks dmenu, registers the read end of the pipe, and returns; the handler runs
when `poll(2)` says there is something to read, reads until `EAGAIN` or the end
of file, and removes itself. A handler that leaves its descriptor readable makes
the loop spin, so it has to drain it.

`poll(2)` reports the socket, not libxcb's own queue. libxcb reads ahead during
a reply wait (the property and atom queries at startup) and during `xcb_flush()`,
so an event can sit in that queue with nothing left to read on the socket. The
wait takes such an event first and returns; the drain dispatches it before
asking libxcb for more.

```c
static xcb_generic_event_t* queued_event = NULL;

void wm_xcb_wait_for_events(void) {
    size_t watched = fd_entry_count;
    struct pollfd fds[2 + watched] = {
        { .fd = xcb_get_file_descriptor(dpy), .events = POLLIN },
        { .fd = signals_wakeup_fd(),          .events = POLLIN },
    };
    for (size_t i = 0; i < watched; i++)
        fds[2 + i] = (struct pollfd){ .fd = fd_entries[i].fd, .events = POLLIN };

    if (xcb_connection_has_error(dpy) != 0) {
        running = 0;
        return;
    }

    /* An event libxcb already read is not visible to poll(2) on the socket. */
    queued_event = xcb_poll_for_queued_event(dpy);
    if (queued_event != NULL)
        return;

    /* A signal handler cannot change what poll() is waiting on: a SIGTERM that
       lands before this call would leave the WM asleep and unstoppable. The
       handler writes a byte to the second fd instead. */
    int timeout_ms = fds[1].fd < 0 ? 250 : -1;
    poll(fds, 2 + watched, timeout_ms);
    signals_drain_wakeups();
}

void handle_xcb_events(void) {
    xcb_generic_event_t* event;

    if (queued_event != NULL) {
        event        = queued_event;
        queued_event = NULL;
        dispatch_event(event);
        free(event);
    }

    /* Drain the queue: poll(2) reports that the socket is readable, and a poll
       on a socket whose events are all in libxcb's own queue waits for the next
       event instead of returning to this loop. */
    while ((event = xcb_poll_for_event(dpy)) != NULL) {
        dispatch_event(event);
        free(event);
    }

    /* A reply is not an event: libxcb keeps it in its queue until somebody
       asks for it, so the deferred reads are drained here. xcb_poll_for_reply()
       reports "not yet" instead of waiting, which is what lets the loop stay a
       loop. */
    client_list_poll_size_hints();

    /* The descriptors the wait covered besides X: a child's pipe that became
       readable. The handler reads what is there and starts work; it does not
       wait, which is why the launcher's read is here and not in the action. */
    wm_loop_dispatch_fd_handlers();

    xcb_flush(dpy);
}

int main(void) {
    // ...
    setup_signals();     // installs the handlers and creates the wakeup pipe
    xcb_handler_init();

    while (running) {
        wm_xcb_wait_for_events();
        handle_xcb_events();
    }

    xcb_handler_shutdown();
    // ...
}
```

---

## Why Components Own Handlers

**Decentralization is intentional.**

In a traditional window manager, there's often a central event dispatcher that routes everything. In our architecture:

1. **Components know what events they care about.** Focus component listens for enter/leave. Bar listens for expose. No central knowledge required.

2. **Multiple components can handle the same event.** Both `fullscreen` and `urgency` components might listen to `PROPERTY_NOTIFY` on the same window.

3. **Adding a new component doesn't modify the event loop.** Register your handler, done.

4. **Testing is easier.** Feed a fake X event directly to the handler. No X server needed for unit tests.

---

## Raw Writer vs Request Writer for X Events

| X Event | Authority | Action |
|---------|-----------|--------|
| `ENTER_NOTIFY` | Reality | `sm_raw_write()` — focus SM MUST reflect this |
| `PROPERTY_NOTIFY` | Reality | `sm_raw_write()` — window property changed |
| `RANDR_NOTIFY` | Reality | `sm_raw_write()` — monitor config changed |
| `KEY_PRESS` | Intent | `hub_send_request()` — user wants to do something |
| `BUTTON_PRESS` | Intent | `hub_send_request()` — user wants to do something |

**The key distinction:**
- Hardware/protocol events → **raw write** (reality is authoritative)
- User input events → **request** (user is asking, not commanding)

---

## Component XCB Handler Example

Here's a complete example of a component that owns its XCB handlers:

```c
// focus-component.h
#ifndef FOCUS_COMPONENT_H
#define FOCUS_COMPONENT_H

#include "component.h"
#include "xcb-handler.h"

extern Component focus_component;

void focus_on_init(void);
void focus_handler(void* event);

#endif // FOCUS_COMPONENT_H

// focus-component.c
#include "focus-component.h"
#include "target.h"
#include "sm.h"

Component focus_component = {
    .name = "focus",
    .accepted_targets = (TargetType[]){ TARGET_TYPE_CLIENT },
    .requests = (RequestType[]){ REQ_CLIENT_FOCUS },
    .events = (EventType[]){ EVT_CLIENT_FOCUSED, EVT_CLIENT_UNFOCUSED },
    .on_init = focus_on_init,
    .executor = focus_executor,
};

void focus_on_init(void) {
    // Register XCB handlers
    xcb_handler_register(XCB_ENTER_NOTIFY, &focus_component, focus_handler);
    xcb_handler_register(XCB_FOCUS_IN, &focus_component, focus_handler);
    xcb_handler_register(XCB_FOCUS_OUT, &focus_component, focus_handler);
}

void focus_handler(void* event) {
    uint8_t type = ((xcb_generic_event_t*)event)->response_type & ~0x80;
    
    if (type == XCB_ENTER_NOTIFY) {
        xcb_enter_notify_event_t* e = (void*)event;
        Client* c = client_by_window(e->event);
        if (!c) return;
        
        // Reality says the pointer entered this window
        StateMachine* sm = client_get_sm(c, "focus");
        sm_raw_write(sm, FOCUS_FOCUSED);
    }
}
```

---

## Testing XCB Handlers

Without a real X server:

```c
void test_focus_handler_enter_notify(void) {
    // Create mock event
    xcb_enter_notify_event_t event = {
        .response_type = XCB_ENTER_NOTIFY,
        .event = mock_window_id,
    };
    
    // Setup: create client with focus SM
    Client* c = client_create(mock_window_id);
    
    // Action: call handler directly
    focus_handler(&event);
    
    // Assert: SM is in FOCUSED state
    StateMachine* sm = client_get_sm(c, "focus");
    assert(sm_get_state(sm) == FOCUS_FOCUSED);
    
    // Assert: EVT_CLIENT_FOCUSED was emitted
    assert(event_bus_received(EVT_CLIENT_FOCUSED));
}
```

---

## Thread Safety

XCB is single-threaded by design. The event loop runs in one thread, dispatching events sequentially. State machines are single-threaded per-instance.

**For v1:** No thread safety concerns. All processing is sequential.

**Future considerations:**
- If we add async XCB replies on a separate thread, SMs would need mutex protection
- Event bus would need mutex if called from multiple threads

---

*See also:*
- `architecture/overview.md` — High-level event flows
- `architecture/component.md` — Component lifecycle and handlers
- `architecture/decisions.md` — Decision: Raw vs Request writers
