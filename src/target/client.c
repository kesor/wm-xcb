/*
 * Client Target Implementation
 *
 * Incorporates:
 * - Sentinel-based linked list pattern from wm-window-list.c
 * - XCB utility functions from wm-clients.c
 */

#include <stdlib.h>
#include <string.h>
#include <xcb/xcbext.h>

#include "../sm/sm-registry.h"
#include "../sm/sm.h"
#include "client.h"
#include "monitor.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-log.h"
#include "src/core/wm-xcb.h"

/*
 * Sentinel-based client list
 *
 * The window manager holds only a small number of windows at any one time,
 * usually less than about a hundred. Thus it is a good idea to optimize for
 * adding and removing windows as quickly as possible.
 *
 * Finding windows should also be fast to react to events that the X server
 * is sending about notifications or creation/destruction of windows.
 */
static Client client_sentinel;

/*
 * The client values a component must not write, each one with mutually
 * exclusive values the WM's behaviour depends on. Defined here and not in
 * client.h: a component's translation unit cannot name these fields, so
 * assigning one is a build error instead of a review finding. The accessors
 * declared in client.h are the only way in.
 */
struct ClientState {
  bool managed; /* added to the window list */
  bool urgent;  /* has the urgency hint */
  bool mapped;  /* is currently mapped */
};

/* Set by client_atoms_init(), read by client_set_wm_state() and by the size
 * hint reader. */
static xcb_atom_t wm_state_atom        = XCB_ATOM_NONE;
static xcb_atom_t wm_normal_hints_atom = XCB_ATOM_NONE;

static void
client_properties_init(Client* c)
{
  c->title        = NULL;
  c->class_name   = NULL;
  c->x            = 0;
  c->y            = 0;
  c->width        = 0;
  c->height       = 0;
  c->border_width = 0;
  c->tags         = 0;
  c->monitor      = NULL;
  c->focusable    = true;
  c->stack_mode   = XCB_STACK_MODE_ABOVE;

  c->tracked->managed = false;
  c->tracked->urgent  = false;
  c->tracked->mapped  = false;

  c->pending_unmaps = 0;

  /* No hints until the window says otherwise: a client that sets none is
   * unconstrained, not zero-sized. */
  memset(&c->size_hints, 0, sizeof(c->size_hints));
  c->size_hints_pending = false;

  /* Initialize SM storage */
  c->sms.machines = NULL;
  c->sms.names    = NULL;
  c->sms.count    = 0;
  c->sms.capacity = 0;
}

/*
 * Helper: find SM index by name.
 * Returns -1 if not found.
 */
static int32_t
client_sm_find(const Client* c, const char* sm_name)
{
  if (c == NULL || sm_name == NULL)
    return -1;

  for (uint32_t i = 0; i < c->sms.count; i++) {
    if (c->sms.names[i] != NULL && strcmp(c->sms.names[i], sm_name) == 0) {
      return (int32_t) i;
    }
  }
  return -1;
}

/*
 * Helper: add or update SM for a name.
 * Takes ownership of `sm` - any previously registered SM for this name
 * will be destroyed (unless it's the same pointer to avoid self-destruction).
 * The sm_name string is copied internally.
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should handle cleanup of the passed SM.
 */
static bool
client_sm_set_internal(Client* c, const char* sm_name, StateMachine* sm)
{
  int32_t idx = client_sm_find(c, sm_name);

  if (idx >= 0) {
    /* Update existing - destroy old SM if different from new one */
    if (c->sms.machines[idx] != NULL && c->sms.machines[idx] != sm) {
      sm_destroy(c->sms.machines[idx]);
    }
    c->sms.machines[idx] = sm;
    return true;
  }

  /* Add new SM */
  if (c->sms.count >= c->sms.capacity) {
    uint32_t       new_capacity = c->sms.capacity == 0 ? 4 : c->sms.capacity * 2;
    StateMachine** new_machines = NULL;
    char**         new_names    = NULL;

    /* Reallocate machines first */
    new_machines = realloc(c->sms.machines, new_capacity * sizeof(StateMachine*));
    if (new_machines == NULL) {
      LOG_ERROR("Failed to expand SM machines storage for client");
      return false;
    }

    /* Reallocate names */
    new_names = realloc(c->sms.names, new_capacity * sizeof(char*));
    if (new_names == NULL) {
      free(new_machines);
      LOG_ERROR("Failed to expand SM names storage for client");
      return false;
    }

    /* Both allocations succeeded, update atomically */
    c->sms.machines = new_machines;
    c->sms.names    = new_names;
    c->sms.capacity = new_capacity;
  }

  /* strdup the name - check for failure */
  char* copied_name = sm_name ? strdup(sm_name) : NULL;
  if (sm_name != NULL && copied_name == NULL) {
    LOG_ERROR("Failed to copy SM name for client");
    /* Don't add the SM if we can't store the name */
    return false;
  }

  c->sms.machines[c->sms.count] = sm;
  c->sms.names[c->sms.count]    = copied_name;
  c->sms.count++;

  return true;
}

/*
 * Initialize the client list.
 * Must be called before using any client list functions.
 */
void
client_list_init(void)
{
  client_sentinel.next = &client_sentinel;
  client_sentinel.prev = &client_sentinel;
}

/*
 * Shutdown the client list.
 * Destroys all clients in the list and cleans up resources.
 */
void
client_list_shutdown(void)
{
  /* Destroy all clients in the list */
  Client* c = client_sentinel.next;
  while (c != &client_sentinel) {
    Client* next = c->next;
    client_destroy(c);
    c = next;
  }
  /* Re-initialize sentinel */
  client_sentinel.next = &client_sentinel;
  client_sentinel.prev = &client_sentinel;
}

/*
 * Get the client list sentinel for iteration.
 * Returns the sentinel node (head/tail of the circular list).
 */
Client*
client_list_sentinel(void)
{
  return &client_sentinel;
}

/*
 * Get the first client in the list.
 * Returns NULL if the list is empty.
 */
Client*
client_list_get_head(void)
{
  if (client_sentinel.next == &client_sentinel)
    return NULL;
  return client_sentinel.next;
}

/*
 * Get the last client in the list.
 */
static Client*
client_list_get_tail(void)
{
  if (client_sentinel.prev == &client_sentinel)
    return NULL;
  return client_sentinel.prev;
}

/*
 * Add a client to the list (inserts at the head, after sentinel).
 */
static void
client_list_add(Client* c)
{
  if (c == NULL)
    return;

  c->next                    = client_sentinel.next;
  c->prev                    = &client_sentinel;
  client_sentinel.next->prev = c;
  client_sentinel.next       = c;
}

/*
 * Remove a client from the list.
 */
static void
client_list_remove(Client* c)
{
  if (c == NULL || c == &client_sentinel)
    return;

  c->prev->next = c->next;
  c->next->prev = c->prev;
  c->prev       = NULL;
  c->next       = NULL;
}

/*
 * Check if the client list is empty.
 */
bool
client_list_is_empty(void)
{
  return client_sentinel.next == &client_sentinel;
}

/*
 * Count clients in the list.
 */
uint32_t
client_list_count(void)
{
  uint32_t count = 0;
  Client*  c     = client_sentinel.next;
  while (c != &client_sentinel) {
    count++;
    c = c->next;
  }
  return count;
}

/*
 * Check if a window exists in the list.
 */
bool
client_list_contains_window(xcb_window_t window)
{
  Client* c = client_sentinel.next;
  while (c != &client_sentinel) {
    if (c->window == window)
      return true;
    c = c->next;
  }
  return false;
}

/*
 * Monitor a new window belongs to. Pointer routing would need motion tracking
 * that does not exist yet, because nothing tracks pointer motion, so the selected
 * monitor wins, with the first monitor in the list as the answer for a tree
 * where nothing is selected.
 */
static Monitor*
client_default_monitor(void)
{
  Monitor* m = monitor_get_selected();
  return m != NULL ? m : monitor_list_get_first();
}

Client*
client_create(xcb_window_t window)
{
  if (window == XCB_NONE) {
    LOG_ERROR("Cannot create client with XCB_NONE window");
    return NULL;
  }

  /* Check for duplicate window ID */
  if (hub_get_target_by_id((TargetID) window) != NULL) {
    LOG_ERROR("Target with ID %u already registered", window);
    return NULL;
  }

  /* Check if already in our list */
  if (client_list_contains_window(window)) {
    LOG_ERROR("Window %u already in client list", window);
    return NULL;
  }

  LOG_DEBUG("Creating client for window: %u", window);

  Client* c = malloc(sizeof(Client));
  if (c == NULL) {
    LOG_ERROR("Failed to allocate Client");
    return NULL;
  }

  c->tracked = malloc(sizeof(ClientState));
  if (c->tracked == NULL) {
    LOG_ERROR("Failed to allocate Client state");
    free(c);
    return NULL;
  }

  /* Initialize base target */
  c->target.id         = (TargetID) window;
  c->target.type_id    = hub_get_target_type_id_by_name("client");
  c->target.registered = false;

  /* Initialize X properties */
  c->window = window;
  client_properties_init(c);

  /* Add to client list */
  client_list_add(c);

  /* Register with Hub */
  hub_register_target(&c->target);

  /* Verify registration succeeded */
  if (!c->target.registered) {
    LOG_ERROR("Failed to register client target for window: %u", window);
    /* ClientState is already allocated. free(c) leaks it; client_destroy
     * frees both, and the client is far enough along for that path: it is on
     * the list, its state-machine slots are empty, and it is not registered. */
    client_destroy(c);
    return NULL;
  }

  /* A client with no monitor is invisible to tiling, so the association is made
   * here rather than left to the layout component. Its tags are assigned by the
   * tag-manager component when the client is managed, from the view that owns
   * them. */
  Monitor* m = client_default_monitor();
  if (m != NULL)
    client_set_monitor(c, m);

  LOG_DEBUG("Client created: window=%u, monitor=%u",
            window, m != NULL ? m->output : 0);
  return c;
}

/*
 * Drop a size hint read whose reply nobody will consume.
 *
 * An unchecked request keeps its reply in libxcb until somebody reads or
 * discards it, and a client can be destroyed or ask again before the reply has
 * arrived.
 */
static void
client_discard_size_hints(Client* c)
{
  if (!c->size_hints_pending)
    return;
  if (dpy != NULL)
    xcb_discard_reply(dpy, c->size_hints_cookie.sequence);
  c->size_hints_pending = false;
}

void
client_destroy(Client* c)
{
  if (c == NULL || c == &client_sentinel)
    return;

  LOG_DEBUG("Destroying client: window=%u", c->window);

  /* Detach from monitor (Monitor tracks by window ID via hub) */
  c->monitor = NULL;

  /* Remove from client list */
  client_list_remove(c);

  /* Destroy all state machines */
  for (uint32_t i = 0; i < c->sms.count; i++) {
    if (c->sms.machines[i] != NULL) {
      sm_destroy(c->sms.machines[i]);
    }
    if (c->sms.names[i] != NULL) {
      free(c->sms.names[i]);
    }
  }
  free(c->sms.machines);
  free(c->sms.names);
  c->sms.machines = NULL;
  c->sms.names    = NULL;
  c->sms.count    = 0;
  c->sms.capacity = 0;

  /* Free X properties */
  if (c->title != NULL) {
    free(c->title);
    c->title = NULL;
  }
  if (c->class_name != NULL) {
    free(c->class_name);
    c->class_name = NULL;
  }

  /* A DestroyNotify can arrive in the same batch as the MapRequest that asked
   * for the hints, so the reply may still be on its way. */
  client_discard_size_hints(c);

  free(c->tracked);
  c->tracked = NULL;

  /* Unregister from Hub */
  if (c->target.registered) {
    hub_unregister_target(c->target.id);
  }

  /* Free client */
  free(c);

  LOG_DEBUG("Client destroyed");
}

/*
 * Destroy a client by window ID.
 */
void
client_destroy_by_window(xcb_window_t window)
{
  Client* c = client_get_by_window(window);
  if (c != NULL && c != &client_sentinel) {
    client_destroy(c);
  }
}

/*
 * Get a client by its window ID from the Hub registry.
 */
Client*
client_get_by_window(xcb_window_t window)
{
  HubTarget* t = hub_get_target_by_id((TargetID) window);
  if (t == NULL || t->type_id != hub_get_target_type_id_by_name("client"))
    return NULL;
  return (Client*) t;
}

/*
 * Iterate over all clients.
 * Safe against callback removing the current client.
 */
void
client_foreach(void (*callback)(Client*))
{
  Client* c = client_sentinel.next;
  while (c != &client_sentinel) {
    Client* next = c->next; /* Save next before callback might free c */
    callback(c);
    c = next;
  }
}

/*
 * Iterate over all clients in reverse order.
 * Safe against callback removing the current client.
 */
void
client_foreach_reverse(void (*callback)(Client*))
{
  Client* c = client_sentinel.prev;
  while (c != &client_sentinel) {
    Client* prev = c->prev; /* Save prev before callback might free c */
    callback(c);
    c = prev;
  }
}

/*
 * Count the number of managed clients.
 */
uint32_t
client_count_managed(void)
{
  uint32_t count = 0;
  Client*  c     = client_sentinel.next;
  while (c != &client_sentinel) {
    if (client_is_managed(c))
      count++;
    c = c->next;
  }
  return count;
}

/*
 * Get the next client in the list.
 */
Client*
client_get_next(const Client* c)
{
  if (c == NULL || c->next == &client_sentinel)
    return NULL;
  return c->next;
}

/*
 * Get the previous client in the list.
 */
Client*
client_get_prev(const Client* c)
{
  if (c == NULL || c->prev == &client_sentinel)
    return NULL;
  return c->prev;
}

/*
 * Get state machine by name.
 * Returns NULL if no SM is registered for this name.
 */
StateMachine*
client_get_sm(Client* c, const char* sm_name)
{
  if (c == NULL || sm_name == NULL)
    return NULL;

  int32_t idx = client_sm_find(c, sm_name);
  if (idx < 0)
    return NULL;

  return c->sms.machines[idx];
}

/*
 * Set a state machine for this client.
 * The SM is stored by name - components can retrieve it later via client_get_sm().
 *
 * Returns true on success, false on failure (OOM).
 * On failure, the caller should destroy the SM to avoid leaks.
 */
bool
client_set_sm(Client* c, const char* sm_name, StateMachine* sm)
{
  if (c == NULL || sm_name == NULL)
    return false;

  return client_sm_set_internal(c, sm_name, sm);
}

/*
 * Check if client can receive focus.
 * Note: This returns the focusable flag, not actual focus state.
 * Actual focus state will be tracked via focus state machine.
 */
bool
client_is_focusable(const Client* c)
{
  if (c == NULL)
    return false;
  return c->focusable;
}

/*
 * Set the focusable flag for this client.
 */
void
client_set_focusable(Client* c, bool focusable)
{
  if (c == NULL)
    return;
  c->focusable = focusable;
}

/*
 * Set the monitor association for this client.
 */
void
client_set_monitor(Client* c, Monitor* m)
{
  if (c == NULL)
    return;

  c->monitor = m;
  /* Note: Monitor tracks client associations by window ID via hub,
   * not by direct pointers. This keeps Monitor and Client decoupled. */
}

/*
 * Get the monitor association for this client.
 */
Monitor*
client_get_monitor(const Client* c)
{
  return c ? c->monitor : NULL;
}

/*
 * Set client title.
 * Ownership of `title` is transferred to the client.
 */
void
client_set_title(Client* c, char* title)
{
  if (c == NULL)
    return;
  if (c->title != NULL)
    free(c->title);
  c->title = title;
}

/*
 * Set client class name.
 * Ownership of `class_name` is transferred to the client.
 */
void
client_set_class(Client* c, char* class_name)
{
  if (c == NULL)
    return;
  if (c->class_name != NULL)
    free(c->class_name);
  c->class_name = class_name;
}

/*
 * Set client geometry.
 */
void
client_set_geometry(Client* c, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
  if (c == NULL)
    return;
  c->x      = x;
  c->y      = y;
  c->width  = width;
  c->height = height;
}

/*
 * Set client border width.
 */
void
client_set_border_width(Client* c, uint16_t border_width)
{
  if (c == NULL)
    return;
  c->border_width = border_width;
}

/*
 * Size hints
 */

void
client_set_size_hints(Client* c, const ClientSizeHints* hints)
{
  if (c == NULL || hints == NULL)
    return;
  c->size_hints = *hints;
}

/* A CARD32 hint into the 16-bit window size X takes. A negative dimension is
 * not a size, and reading it as unsigned would turn it into 65535. */
static uint16_t
client_hint_dimension(int32_t value)
{
  if (value <= 0)
    return 0;
  return (uint16_t) (value > UINT16_MAX ? UINT16_MAX : value);
}

bool
client_size_hints_from_property(const int32_t* values, uint32_t count, ClientSizeHints* out)
{
  if (values == NULL || out == NULL)
    return false;

  /* Only the two lengths WM_SIZE_HINTS has: the 18 slots Xlib writes and the
   * 15 pre-ICCCM slots. A shorter property is not one, and reading slots the
   * client never wrote would size the window from the server's leftovers. */
  if (count != CLIENT_SIZE_HINT_OLD_LENGTH && count < CLIENT_SIZE_HINT_LENGTH)
    return false;

  uint32_t flags = (uint32_t) values[CLIENT_SIZE_HINT_FLAGS];

  /* Fixed offsets: the fields are where xPropSizeHints puts them whether or
   * not their flag is set, which is why a property of all-zero position and
   * size still has min_width at slot 5. Only a flag decides a field is used. */
  ClientSizeHints parsed;
  memset(&parsed, 0, sizeof(parsed));

  if ((flags & CLIENT_SIZE_HINT_P_MIN_SIZE) != 0) {
    parsed.min_width  = client_hint_dimension(values[CLIENT_SIZE_HINT_MIN_WIDTH]);
    parsed.min_height = client_hint_dimension(values[CLIENT_SIZE_HINT_MIN_HEIGHT]);
  }
  if ((flags & CLIENT_SIZE_HINT_P_MAX_SIZE) != 0) {
    parsed.max_width  = client_hint_dimension(values[CLIENT_SIZE_HINT_MAX_WIDTH]);
    parsed.max_height = client_hint_dimension(values[CLIENT_SIZE_HINT_MAX_HEIGHT]);
  }
  if ((flags & CLIENT_SIZE_HINT_P_RESIZE_INC) != 0) {
    parsed.inc_width  = client_hint_dimension(values[CLIENT_SIZE_HINT_WIDTH_INC]);
    parsed.inc_height = client_hint_dimension(values[CLIENT_SIZE_HINT_HEIGHT_INC]);
  }
  if ((flags & CLIENT_SIZE_HINT_P_BASE_SIZE) != 0) {
    /* The 15-slot form stops before the base size, so a property that names
     * one is malformed rather than unconstrained. */
    if (count < CLIENT_SIZE_HINT_LENGTH)
      return false;
    parsed.base_width  = client_hint_dimension(values[CLIENT_SIZE_HINT_BASE_WIDTH]);
    parsed.base_height = client_hint_dimension(values[CLIENT_SIZE_HINT_BASE_HEIGHT]);
  }

  *out = parsed;
  return true;
}

void
client_request_size_hints(Client* c)
{
  if (c == NULL || dpy == NULL || wm_normal_hints_atom == XCB_ATOM_NONE)
    return;

  client_discard_size_hints(c);

  /* Unchecked: an error must not travel to the event loop as a synthetic
   * event, and a window that vanished can only fail here. The longest
   * WM_SIZE_HINTS is 18 CARD32s; asking for that is asking for all of it. */
  c->size_hints_cookie = xcb_get_property_unchecked(
      dpy, 0, c->window, wm_normal_hints_atom, XCB_GET_PROPERTY_TYPE_ANY, 0, 18);
  c->size_hints_pending = true;
}

bool
client_size_hints_equal(const ClientSizeHints* a, const ClientSizeHints* b)
{
  if (a == NULL || b == NULL)
    return a == b;

  return a->min_width == b->min_width && a->min_height == b->min_height &&
         a->max_width == b->max_width && a->max_height == b->max_height &&
         a->base_width == b->base_width && a->base_height == b->base_height &&
         a->inc_width == b->inc_width && a->inc_height == b->inc_height;
}

bool
client_is_normal_hints_atom(xcb_atom_t atom)
{
  return atom != XCB_ATOM_NONE && atom == wm_normal_hints_atom;
}

bool
client_poll_size_hints(Client* c)
{
  if (c == NULL || !c->size_hints_pending)
    return false;

  if (dpy == NULL) {
    c->size_hints_pending = false;
    return false;
  }

  void*                raw_reply = NULL;
  xcb_generic_error_t* error     = NULL;

  /* Not the reply-forcing call: this one reports "not yet" instead of waiting,
   * so the event loop stays free. */
  if (!xcb_poll_for_reply(dpy, c->size_hints_cookie.sequence, &raw_reply, &error)) {
    return false;
  }

  c->size_hints_pending = false;

  xcb_get_property_reply_t* reply = (xcb_get_property_reply_t*) raw_reply;
  ClientSizeHints           parsed;
  bool                      have = false;

  if (reply != NULL && error == NULL) {
    if (reply->format == 0) {
      /* Format 0 is the server saying the property is not there: the client
       * removed the hints, so it has no constraints any more. Keeping the old
       * ones would hold the window at a minimum it no longer asks for. */
      memset(&parsed, 0, sizeof(parsed));
      have = true;
    } else if (reply->format == 32) {
      uint32_t count = (uint32_t) (xcb_get_property_value_length(reply) / (int) sizeof(uint32_t));

      have = client_size_hints_from_property(
          (const int32_t*) xcb_get_property_value(reply), count, &parsed);
    }
  } else if (error != NULL) {
    LOG_DEBUG("WM_NORMAL_HINTS read failed for window %u", c->window);
  }

  free(error);
  free(reply);

  if (!have)
    return false;

  bool changed = !client_size_hints_equal(&c->size_hints, &parsed);
  client_set_size_hints(c, &parsed);
  return changed;
}

/* One dimension against the client's hints. */
static uint16_t
client_clamp_to_hints(uint32_t size, uint16_t min, uint16_t max, uint16_t base, uint16_t inc)
{
  /* ICCCM 4.1.2.3, as Xlib's XWMGeometry reads it: a client that sets only
   * PMinSize means that size to be the base as well, and a client that sets
   * only PBaseSize means it to be the minimum. A field the client left out is
   * zero here, so the two defaults are each other. */
  if (base == 0)
    base = min;
  if (min == 0)
    min = base;

  /* The increment counts from the base size, so the part above the base is
   * what has to be a multiple of it. Rounding down keeps the window inside
   * the area the layout gave it; the minimum then wins over both. */
  if (inc > 1 && size > base)
    size = base + ((size - base) / inc) * inc;
  if (max > 0 && size > max)
    size = max;
  if (min > 0 && size < min)
    size = min;

  return (uint16_t) (size == 0 ? 1 : size);
}

void
client_apply_size_hints(const Client* c, uint16_t* width, uint16_t* height)
{
  if (c == NULL || width == NULL || height == NULL)
    return;

  *width = client_clamp_to_hints(*width,
                                 c->size_hints.min_width,
                                 c->size_hints.max_width,
                                 c->size_hints.base_width,
                                 c->size_hints.inc_width);

  *height = client_clamp_to_hints(*height,
                                  c->size_hints.min_height,
                                  c->size_hints.max_height,
                                  c->size_hints.base_height,
                                  c->size_hints.inc_height);
}

/*
 * Set tags for the client.
 */
void
client_set_tags(Client* c, uint32_t tags)
{
  if (c == NULL)
    return;
  c->tags = tags;
}

/*
 * Add a tag to the client.
 * Tag must be less than 32.
 */
void
client_add_tag(Client* c, uint32_t tag)
{
  if (c == NULL || tag >= 32)
    return;
  c->tags |= (1U << tag);
}

/*
 * Remove a tag from the client.
 * Tag must be less than 32.
 */
void
client_remove_tag(Client* c, uint32_t tag)
{
  if (c == NULL || tag >= 32)
    return;
  c->tags &= ~(1U << tag);
}

/*
 * Check if client has a specific tag.
 * Tag must be less than 32.
 */
bool
client_has_tag(const Client* c, uint32_t tag)
{
  if (c == NULL || tag >= 32)
    return false;
  return (c->tags & (1U << tag)) != 0;
}

/*
 * Set urgent state.
 */
void
client_set_urgent(Client* c, bool urgent)
{
  if (c == NULL || c->tracked == NULL)
    return;
  c->tracked->urgent = urgent;
}

/*
 * Check if client is urgent.
 */
bool
client_is_urgent(const Client* c)
{
  return c != NULL && c->tracked != NULL ? c->tracked->urgent : false;
}

/*
 * Set managed state.
 */
void
client_set_managed(Client* c, bool managed)
{
  if (c == NULL || c->tracked == NULL)
    return;
  c->tracked->managed = managed;
}

/*
 * Check if client is managed.
 */
bool
client_is_managed(const Client* c)
{
  return c != NULL && c->tracked != NULL ? c->tracked->managed : false;
}

/*
 * Set mapped state.
 */
void
client_set_mapped(Client* c, bool mapped)
{
  if (c == NULL || c->tracked == NULL)
    return;
  c->tracked->mapped = mapped;
}

/*
 * Check if client is mapped.
 */
bool
client_is_mapped(const Client* c)
{
  return c != NULL && c->tracked != NULL ? c->tracked->mapped : false;
}

/*
 * Spend one unmap the WM recorded in client_hide().
 */
bool
client_take_pending_unmap(Client* c)
{
  if (c == NULL || c->pending_unmaps == 0)
    return false;

  c->pending_unmaps--;
  return true;
}

/*
 * Set stack mode.
 */
void
client_set_stack_mode(Client* c, enum xcb_stack_mode_t stack_mode)
{
  if (c == NULL)
    return;
  c->stack_mode = stack_mode;
}

/*
 * Get stack mode.
 */
enum xcb_stack_mode_t
client_get_stack_mode(const Client* c)
{
  return c ? c->stack_mode : XCB_STACK_MODE_ABOVE;
}

/*
 * XCB Utility Functions
 * Moved from wm-clients.c to consolidate client functionality
 */

/*
 * Configure a client window (position, size, border, stack order).
 */
xcb_void_cookie_t
client_configure(
    xcb_window_t          wnd,
    int16_t               x,
    int16_t               y,
    uint16_t              width,
    uint16_t              height,
    uint16_t              border_width,
    enum xcb_stack_mode_t stack_mode)
{
  /* Unit tests leave dpy NULL. xcb_configure_window(NULL, ...) crashes, and
   * there is no server to configure. */
  if (dpy == NULL)
    return (xcb_void_cookie_t) { 0 };

  uint32_t value_list[] = { (uint32_t) x, (uint32_t) y, width, height, border_width, (uint32_t) stack_mode };

  const uint16_t value_mask =
      XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
      XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
      XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_STACK_MODE;

  return xcb_configure_window(dpy, wnd, value_mask, value_list);
}

/*
 * Configure a client from its struct.
 */
xcb_void_cookie_t
client_configure_from_struct(const Client* c)
{
  if (c == NULL)
    return (xcb_void_cookie_t) { 0 };

  return client_configure(
      c->window,
      c->x,
      c->y,
      c->width,
      c->height,
      c->border_width,
      c->stack_mode);
}

/*
 * Show a client window (map it).
 */
xcb_void_cookie_t
client_show(xcb_window_t wnd)
{
  if (dpy == NULL)
    return (xcb_void_cookie_t) { 0 };
  return xcb_map_window(dpy, wnd);
}

/*
 * Intern one atom. Startup only: a reply is a round trip, and the event loop
 * does not exist yet.
 */
static xcb_atom_t
client_intern_atom(const char* name)
{
  /* allow-blocking: startup, the event loop has not started yet */
  xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(dpy, xcb_intern_atom(dpy, 0, (uint16_t) strlen(name), name), NULL);
  if (reply == NULL) {
    LOG_ERROR("Failed to intern the %s atom", name);
    return XCB_ATOM_NONE;
  }

  xcb_atom_t atom = reply->atom;
  free(reply);
  return atom;
}

/*
 * Intern the atoms the client code needs but xcb does not predefine.
 */
void
client_atoms_init(void)
{
  if (dpy == NULL)
    return;

  if (wm_state_atom == XCB_ATOM_NONE)
    wm_state_atom = client_intern_atom("WM_STATE");

  if (wm_normal_hints_atom == XCB_ATOM_NONE)
    wm_normal_hints_atom = client_intern_atom("WM_NORMAL_HINTS");
}

/*
 * Set the ICCCM WM_STATE property on a window.
 * The value is a pair: the state, then the icon window (None, no icon support).
 */
bool
client_set_wm_state(xcb_window_t wnd, enum ClientWMState state)
{
  if (dpy == NULL || wm_state_atom == XCB_ATOM_NONE)
    return false;

  uint32_t values[] = { (uint32_t) state, XCB_NONE };

  xcb_change_property(
      dpy, XCB_PROP_MODE_REPLACE, wnd, wm_state_atom, wm_state_atom, 32, 2, values);

  return true;
}

/*
 * Hide a client window (unmap it).
 *
 * The UnmapNotify that comes back cannot be told from a withdrawal by the
 * state it finds: a tag change can hide a window and show it again before the
 * loop reads the notify, so `mapped` is true by then. Record the request here,
 * where the WM makes it, and let the client-list handler spend it.
 */
xcb_void_cookie_t
client_hide(xcb_window_t wnd)
{
  Client* c = client_get_by_window(wnd);

  /* A window the server already has unmapped produces no notify, so only a
   * hide of a window the WM believes is on screen owes one. */
  if (c != NULL && client_is_mapped(c))
    c->pending_unmaps++;

  if (dpy == NULL)
    return (xcb_void_cookie_t) { 0 };
  return xcb_unmap_window(dpy, wnd);
}
