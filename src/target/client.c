/*
 * Client Target Implementation
 *
 * Incorporates:
 * - Sentinel-based linked list pattern from wm-window-list.c
 * - XCB utility functions from wm-clients.c
 */

#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "../../wm-hub.h"
#include "../sm/sm.h"
#include "../sm/sm-registry.h"
#include "../../wm-log.h"
#include "../../wm-xcb.h"
#include "monitor.h"

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

static void
client_properties_init(Client* c)
{
  c->title         = NULL;
  c->class_name    = NULL;
  c->x             = 0;
  c->y             = 0;
  c->width         = 0;
  c->height        = 0;
  c->border_width  = 0;
  c->tags          = 0;
  c->monitor       = NULL;
  c->managed       = false;
  c->urgent        = false;
  c->focusable     = true;
  c->mapped        = false;
  c->stack_mode    = XCB_STACK_MODE_ABOVE;
  c->sms.fullscreen = NULL;
  c->sms.floating   = NULL;
  c->sms.urgency    = NULL;
  c->sms.focus      = NULL;
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

  c->next            = client_sentinel.next;
  c->prev            = &client_sentinel;
  client_sentinel.next->prev = c;
  client_sentinel.next = c;
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
  c->prev = NULL;
  c->next = NULL;
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

  /* Initialize base target */
  c->target.id         = (TargetID) window;
  c->target.type       = TARGET_TYPE_CLIENT;
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
    client_list_remove(c);
    free(c);
    return NULL;
  }

  LOG_DEBUG("Client created: window=%u", window);
  return c;
}

void
client_destroy(Client* c)
{
  if (c == NULL || c == &client_sentinel)
    return;

  LOG_DEBUG("Destroying client: window=%u", c->window);

  /* Detach from monitor if associated */
  if (c->monitor != NULL && c->monitor->clients == c) {
    c->monitor->clients = NULL;
  }
  c->monitor = NULL;

  /* Remove from client list */
  client_list_remove(c);

  /* Destroy all state machines */
  if (c->sms.fullscreen != NULL) {
    sm_destroy(c->sms.fullscreen);
    c->sms.fullscreen = NULL;
  }
  if (c->sms.floating != NULL) {
    sm_destroy(c->sms.floating);
    c->sms.floating = NULL;
  }
  if (c->sms.urgency != NULL) {
    sm_destroy(c->sms.urgency);
    c->sms.urgency = NULL;
  }
  if (c->sms.focus != NULL) {
    sm_destroy(c->sms.focus);
    c->sms.focus = NULL;
  }

  /* Free X properties */
  if (c->title != NULL) {
    free(c->title);
    c->title = NULL;
  }
  if (c->class_name != NULL) {
    free(c->class_name);
    c->class_name = NULL;
  }

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
  if (t == NULL || t->type != TARGET_TYPE_CLIENT)
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
    Client* next = c->next;  /* Save next before callback might free c */
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
    Client* prev = c->prev;  /* Save prev before callback might free c */
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
    if (c->managed)
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
 * Get state machine by name (on-demand allocation).
 */
StateMachine*
client_get_sm(Client* c, const char* sm_name)
{
  if (c == NULL || sm_name == NULL)
    return NULL;

  if (strcmp(sm_name, "fullscreen") == 0) {
    if (c->sms.fullscreen == NULL) {
      LOG_DEBUG("Fullscreen SM requested but not yet implemented");
    }
    return c->sms.fullscreen;
  }

  if (strcmp(sm_name, "floating") == 0) {
    if (c->sms.floating == NULL) {
      LOG_DEBUG("Floating SM requested but not yet implemented");
    }
    return c->sms.floating;
  }

  if (strcmp(sm_name, "urgency") == 0) {
    if (c->sms.urgency == NULL) {
      LOG_DEBUG("Urgency SM requested but not yet implemented");
    }
    return c->sms.urgency;
  }

  if (strcmp(sm_name, "focus") == 0) {
    if (c->sms.focus == NULL) {
      LOG_DEBUG("Focus SM requested but not yet implemented");
    }
    return c->sms.focus;
  }

  LOG_WARN("Unknown state machine requested: %s", sm_name);
  return NULL;
}

/*
 * Set a state machine for this client.
 */
void
client_set_sm(Client* c, const char* sm_name, StateMachine* sm)
{
  if (c == NULL || sm_name == NULL)
    return;

  if (strcmp(sm_name, "fullscreen") == 0) {
    c->sms.fullscreen = sm;
  } else if (strcmp(sm_name, "floating") == 0) {
    c->sms.floating = sm;
  } else if (strcmp(sm_name, "urgency") == 0) {
    c->sms.urgency = sm;
  } else if (strcmp(sm_name, "focus") == 0) {
    c->sms.focus = sm;
  } else {
    LOG_WARN("Cannot set unknown state machine: %s", sm_name);
  }
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

  /* Detach from the previous monitor if it still points back to this client */
  if (c->monitor != NULL && c->monitor->clients == c) {
    c->monitor->clients = NULL;
  }

  c->monitor = m;

  /* Attach to the new monitor */
  if (m != NULL && m->clients == NULL) {
    m->clients = c;
  }
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
  c->tags |= (1u << tag);
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
  c->tags &= ~(1u << tag);
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
  return (c->tags & (1u << tag)) != 0;
}

/*
 * Set urgent state.
 */
void
client_set_urgent(Client* c, bool urgent)
{
  if (c == NULL)
    return;
  c->urgent = urgent;
}

/*
 * Check if client is urgent.
 */
bool
client_is_urgent(const Client* c)
{
  return c ? c->urgent : false;
}

/*
 * Set managed state.
 */
void
client_set_managed(Client* c, bool managed)
{
  if (c == NULL)
    return;
  c->managed = managed;
}

/*
 * Check if client is managed.
 */
bool
client_is_managed(const Client* c)
{
  return c ? c->managed : false;
}

/*
 * Set mapped state.
 */
void
client_set_mapped(Client* c, bool mapped)
{
  if (c == NULL)
    return;
  c->mapped = mapped;
}

/*
 * Check if client is mapped.
 */
bool
client_is_mapped(const Client* c)
{
  return c ? c->mapped : false;
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
  return xcb_map_window(dpy, wnd);
}

/*
 * Hide a client window (unmap it).
 */
xcb_void_cookie_t
client_hide(xcb_window_t wnd)
{
  return xcb_unmap_window(dpy, wnd);
}
