# Extension: Attach Below / Aside

## Overview

Controls how new clients are inserted into the client list. The default dwm prepends clients to the head; this extension allows alternative insertion policies:
- **Attach Below**: Insert after the focused client
- **Attach Aside**: Insert after focused client but maintain focus order
- **Attach After Window**: Insert at specific position

## DWM Reference

Based on `attach.md`, `manage.md`:
- `attach()` prepends to head (newest on top)
- `attachstack()` prepends to stack
- `attachBelow()` — custom patch in this dwm fork
- `manage()` calls `attach(c)` then `attachstack(c)`

## State Machine Events

| Event | Description | Payload |
|-------|-------------|---------|
| `EVT_CLIENT_MANAGED` | Client being registered | `Client*`, `Monitor*` |
| `EVT_CLIENT_INSERTED` | Client inserted into list | `Client*`, `position` |

## Insertion Policies

```c
enum InsertPolicy {
    INSERT_HEAD,      // Prepend (dwm default)
    INSERT_TAIL,      // Append
    INSERT_AFTER_SEL, // After focused client
    INSERT_BELOW_SEL, // Below focused client in tiling order
};

typedef struct {
    enum InsertPolicy policy;
    int preserve_focus;   // Keep focus on current client
} AttachConfig;
```

## Hook Points

```c
void on_client_managed(const Event* evt, void* userdata) {
    Client* c = evt->subject;
    AttachConfig* cfg = userdata;
    
    switch (cfg->policy) {
    case INSERT_AFTER_SEL:
        if (c->mon->sel && c->mon->sel->isfloating) {
            // Insert after selected client
            Client* sel = c->mon->sel;
            c->next = sel->next;
            sel->next = c;
        } else {
            attach(c);  // Default prepend
        }
        break;
    case INSERT_TAIL:
        attach_tail(c);
        break;
    default:
        attach(c);
    }
}
```

## Interactions

- Affects tiling order (which client gets tiled first)
- Works with `pertag` (focus tracking per tag)
- `attachBelow()` custom function from dwm patch

---

*Extension ID: attach-policy*
*Priority: Low (specific use case)*
*DWM patch equivalent: dwm-attach-aside-and-below*