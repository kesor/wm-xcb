#include <stdlib.h>
#include <xcb/xcb.h>

/*
 * The window manager holds only a small number of windows at any one time,
 * usually less than about a hundred. Thus it is a good idea to optimize for
 * adding and removing windows as quickly as possible.
 *
 * Finding windows should also be fast to react to events that the X server
 * is sending about notifications or creation/destruction of windows.
 *
 **/

#include "wm-log.h"
#include "wm-window-list.h"

static wnd_node_t sentinel;

void
window_properties_init(struct wnd_node_t* wnd)
{
  wnd->title        = NULL;
  wnd->parent       = XCB_NONE;
  wnd->x            = 0;
  wnd->y            = 0;
  wnd->width        = 0;
  wnd->height       = 0;
  wnd->border_width = 0;
  wnd->stack_mode   = XCB_STACK_MODE_ABOVE;
  wnd->mapped       = 0;
}

wnd_node_t*
get_wnd_list_sentinel()
{
  return &sentinel;
}

void
free_wnd_node(wnd_node_t* wnd)
{
  if (wnd == NULL || wnd == &sentinel)
    return;
  free(wnd);
}

wnd_node_t*
allocate_wnd_node(xcb_window_t window)
{
  wnd_node_t* new_node = malloc(sizeof(wnd_node_t));
  new_node->window     = window;
  window_properties_init(new_node);
  return new_node;
}

wnd_node_t*
window_insert(xcb_window_t window)
{
  if (window == XCB_NONE)
    return XCB_NONE;

  LOG_DEBUG("Inserting a wnd to list: %d", window);

  /* avoid duplicates */
  wnd_node_t* node = window_find(window);
  if (node != NULL)
    return node;

  wnd_node_t* new_node = allocate_wnd_node(window);
  new_node->next       = sentinel.next;
  new_node->prev       = &sentinel;
  sentinel.next->prev  = new_node;
  sentinel.next        = new_node;
  return new_node;
}

void
window_remove(xcb_window_t window)
{
  wnd_node_t* curr = sentinel.next;
  while (curr->window != window && curr != &sentinel)
    curr = curr->next;
  if (curr == &sentinel)
    return;
  curr->prev->next = curr->next;
  curr->next->prev = curr->prev;
  free_wnd_node(curr);
}

wnd_node_t*
window_find(xcb_window_t window)
{
  wnd_node_t* curr = sentinel.next;
  while (curr != &sentinel) {
    if (curr->window == window)
      return curr;
    curr = curr->next;
  }
  return NULL;    // window not found
}

wnd_node_t*
window_get_next(wnd_node_t* wnd)
{
  return wnd->next;
}

wnd_node_t*
window_get_prev(wnd_node_t* wnd)
{
  return wnd->prev;
}

wnd_node_t*
window_list_end()
{
  return &sentinel;
}

void
window_foreach(void (*callback)(wnd_node_t*))
{
  wnd_node_t* curr = sentinel.next;
  while (curr != &sentinel) {
    wnd_node_t* next = curr->next;    // for those times when callback = free()
    callback(curr);
    curr = next;
  }
}

void
setup_window_list()
{
  sentinel.prev = &sentinel;
  sentinel.next = &sentinel;
}

void
destruct_window_list()
{
  window_foreach(free_wnd_node);
}

void
window_focus_set(xcb_window_t window)
{
  if (window == XCB_NONE)
    return;
}

void
window_focus_clear(xcb_window_t window)
{
}

/*
 * Client list implementation
 *
 * This is a separate list from the window list (wnd_node_t).
 * Client represents managed windows with WM-specific state.
 *
 * Ownership: This list does NOT own the Client objects. It maintains
 * linkage for iteration only. Callers are responsible for allocating
 * and freeing Client objects.
 */

/* Client list sentinel (circular list) */
static Client client_sentinel;

void
client_list_init(void)
{
  client_sentinel.next = &client_sentinel;
}

void
client_list_shutdown(void)
{
  /* Clear the list without freeing Client objects.
   * Ownership is not established by this module. */
  Client* current = client_sentinel.next;
  while (current != &client_sentinel) {
    Client* next = current->next;
    current->next = NULL;
    current = next;
  }
  client_sentinel.next = &client_sentinel;
}

Client*
client_list_get_first(void)
{
  if (client_sentinel.next == &client_sentinel) {
    return NULL;
  }
  return client_sentinel.next;
}

Client*
client_list_get_next(Client* c)
{
  if (c == NULL || c->next == &client_sentinel) {
    return NULL;
  }
  return c->next;
}

void
client_list_add(Client* c)
{
  if (c == NULL) {
    return;
  }
  /* Insert at head of circular list */
  c->next            = client_sentinel.next;
  client_sentinel.next = c;
}

void
client_list_remove(Client* c)
{
  if (c == NULL) {
    return;
  }
  /* Find previous node */
  Client* prev = &client_sentinel;
  while (prev->next != &client_sentinel && prev->next != c) {
    prev = prev->next;
  }
  if (prev->next == c) {
    prev->next = c->next;
  }
}

Client*
client_list_find(xcb_window_t window)
{
  Client* current = client_sentinel.next;
  while (current != &client_sentinel) {
    if (current->window == window) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}
