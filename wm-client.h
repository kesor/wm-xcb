#ifndef _WM_CLIENT_H_
#define _WM_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>

#include "wm-hub.h"

/*
 * Client Target
 *
 * Represents a managed window. Each Client is a HubTarget that can:
 * - Register with the Hub as a TARGET_TYPE_CLIENT
 * - Adopt compatible components (focus, fullscreen, floating, urgency, etc.)
 * - Own state machines for its states
 */

#define CLIENT_NAME_MAX 256

/* Forward declarations */
typedef struct Client Client;
typedef struct ClientComponent ClientComponent;

/* Client flags */
typedef uint32_t ClientFlags;
enum {
  CLIENT_FLAG_MANAGED     = 1 << 0,  /* Window is being managed */
  CLIENT_FLAG_FOCUSED     = 1 << 1,  /* Client currently has focus */
  CLIENT_FLAG_URGENT      = 1 << 2,  /* Client has urgency hint */
  CLIENT_FLAG_FLOATING    = 1 << 3,  /* Client is floating */
  CLIENT_FLAG_FULLSCREEN  = 1 << 4,  /* Client is fullscreen */
  CLIENT_FLAG_HIDDEN      = 1 << 5,  /* Client is hidden (not on current tag) */
};

/* Client state machine states */
enum {
  CLIENT_STATE_NORMAL    = 0,
  CLIENT_STATE_FOCUSED   = 1,
  CLIENT_STATE_URGENT    = 2,
  CLIENT_STATE_FLOATING  = 3,
  CLIENT_STATE_FULLSCREEN = 4,
  CLIENT_STATE_HIDDEN    = 5,
};

/* Client structure - represents a managed window */
struct Client {
  /* Hub target integration */
  HubTarget base;  /* id = window, type = TARGET_TYPE_CLIENT */

  /* X11 window properties */
  xcb_window_t window;
  char         name[CLIENT_NAME_MAX];
  char         class_name[CLIENT_NAME_MAX];

  /* Geometry */
  int16_t     x, y;
  uint16_t    width, height;
  uint16_t    border_width;

  /* State */
  ClientFlags flags;
  uint32_t    tags;           /* Bitmask of tags this client is on */

  /* Hierarchy */
  Client*     next;           /* Next client in list */
  Client*     prev;           /* Previous client in list */

  /* Adoption - components adopted by this client */
  ClientComponent* components;
};

/* Client component - attached to a client */
struct ClientComponent {
  const char*        name;
  void*              data;      /* Component-specific data */
  ClientComponent*   next;
};

/*
 * Client lifecycle
 */

/* Create a new client for a window */
Client* client_create(xcb_window_t window);

/* Destroy a client */
void client_destroy(Client* client);

/* Register client with hub */
void client_register(Client* client);

/* Unregister client from hub */
void client_unregister(Client* client);

/*
 * Client modification
 */

/* Update client geometry */
void client_set_geometry(Client* client, int16_t x, int16_t y, uint16_t width, uint16_t height);

/* Update client name */
void client_set_name(Client* client, const char* name);

/* Update client class */
void client_set_class(Client* client, const char* class_name);

/* Set client tags */
void client_set_tags(Client* client, uint32_t tags);

/* Add a tag to client */
void client_add_tag(Client* client, uint32_t tag);

/* Remove a tag from client */
void client_remove_tag(Client* client, uint32_t tag);

/* Check if client has a tag */
bool client_has_tag(Client* client, uint32_t tag);

/* Set client flags */
void client_set_flags(Client* client, ClientFlags flags);

/* Clear client flags */
void client_clear_flags(Client* client, ClientFlags flags);

/* Check client flags */
bool client_has_flags(Client* client, ClientFlags flags);

/*
 * Component adoption
 */

/* Client adopts a component */
void client_adopt(Client* client, ClientComponent* component);

/* Client drops a component */
void client_drop(Client* client, const char* component_name);

/* Get adopted component by name */
ClientComponent* client_get_component(Client* client, const char* name);

/*
 * Query
 */

/* Get client by window ID */
Client* client_get_by_window(xcb_window_t window);

/* Get next client in list */
Client* client_next(Client* client);

/* Get previous client in list */
Client* client_prev(Client* client);

/*
 * Client list management
 */

/* Get the client list head */
Client* client_list_head(void);

/* Get client count */
uint32_t client_count(void);

/*
 * Request types for client operations
 *
 * Components that handle these should register with the Hub
 * and the Client will route requests through the Hub.
 */
enum ClientRequestType {
  REQ_CLIENT_FOCUS        = 100,  /* Focus this client */
  REQ_CLIENT_UNFOCUS      = 101,  /* Unfocus this client */
  REQ_CLIENT_CLOSE        = 102,  /* Close this client */
  REQ_CLIENT_FULLSCREEN   = 103,  /* Toggle fullscreen */
  REQ_CLIENT_FLOAT        = 104,  /* Toggle floating */
  REQ_CLIENT_TILE         = 105,  /* Toggle tiled */
  REQ_CLIENT_TAG          = 106,  /* Set tags */
  REQ_CLIENT_TAG_TOGGLE   = 107,  /* Toggle tags */
  REQ_CLIENT_CLEAR_URGENT = 108,  /* Clear urgency */
};

/*
 * Event types emitted by clients
 */
enum ClientEventType {
  EVT_CLIENT_CREATED     = 200,  /* Client was created */
  EVT_CLIENT_DESTROYED   = 201,  /* Client was destroyed */
  EVT_CLIENT_FOCUSED    = 202,  /* Client gained focus */
  EVT_CLIENT_UNFOCUSED  = 203,  /* Client lost focus */
  EVT_CLIENT_GEOMETRY    = 204,  /* Client geometry changed */
  EVT_CLIENT_NAME        = 205,  /* Client name changed */
  EVT_CLIENT_TAGS        = 206,  /* Client tags changed */
  EVT_CLIENT_URGENT      = 207,  /* Client became urgent */
  EVT_CLIENT_CALM       = 208,  /* Client urgency cleared */
  EVT_CLIENT_FULLSCREEN_ENTERED = 209,
  EVT_CLIENT_FULLSCREEN_EXITED  = 210,
  EVT_CLIENT_FLOATING   = 211,
  EVT_CLIENT_TILED      = 212,
};

#endif /* _WM_CLIENT_H_ */
