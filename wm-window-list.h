#ifndef _WM_WINDOW_LIST_H_
#define _WM_WINDOW_LIST_H_

#include <xcb/xcb.h>
#include <stdbool.h>

/*
 * Client structure
 *
 * Represents a managed X window. Minimal definition for Monitor integration.
 * Extended client properties are managed by the client component.
 *
 * Note: The `next` pointer is used for per-monitor client lists.
 * Clients are linked via their `next` field within each Monitor's client list.
 * The client_list_* functions provide global list operations, but ownership
 * of Client objects remains with the creating component (typically the
 * client management component, not this module).
 */
typedef struct Client {
  struct Client* next; /* Next client in this monitor's client list */

  xcb_window_t window;   /* X window ID */
  uint32_t     tags;     /* Tag mask for this client */
  bool         managed;  /* Whether client is managed by WM */
} Client;

typedef struct wnd_node_t {
  struct wnd_node_t* next;
  struct wnd_node_t* prev;

  char*                 title;
  xcb_window_t          parent;
  xcb_window_t          window;
  int16_t               x;
  int16_t               y;
  uint16_t              width;
  uint16_t              height;
  uint16_t              border_width;
  enum xcb_stack_mode_t stack_mode;

  uint8_t mapped;
} wnd_node_t;

void        setup_window_list();
void        destruct_window_list();
void        free_wnd_node(wnd_node_t* wnd);
void        window_remove(xcb_window_t window);
wnd_node_t* window_insert(xcb_window_t window);
wnd_node_t* window_find(xcb_window_t window);
wnd_node_t* window_get_next(wnd_node_t* wnd);
wnd_node_t* window_get_prev(wnd_node_t* wnd);
wnd_node_t* window_list_end();
void        window_foreach(void (*callback)(wnd_node_t*));
wnd_node_t* get_wnd_list_sentinel();

/*
 * Client list iteration (for Monitor)
 *
 * Client is a separate concept from wnd_node_t.
 * This provides iteration over the managed client list.
 * Returns the next client in the list, or NULL.
 */
Client* client_list_get_next(Client* c);
Client* client_list_get_first(void);

/*
 * Client list management
 */
void client_list_init(void);
void client_list_shutdown(void);
void client_list_add(Client* c);
void client_list_remove(Client* c);
Client* client_list_find(xcb_window_t window);

#endif
