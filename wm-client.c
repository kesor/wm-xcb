#include "wm-client.h"

#include <stdlib.h>
#include <string.h>

#include "wm-log.h"

/* Client list (sentinel-based circular doubly-linked list) */
static Client clients_sentinel = {
  .next = &clients_sentinel,
  .prev = &clients_sentinel,
};

/* Client count */
static uint32_t clients_count = 0;

/* Hash map for window -> client lookup */
#define CLIENT_WINDOW_MAP_SIZE 512

typedef struct client_window_entry {
  xcb_window_t     window;
  Client*          client;
  struct client_window_entry* next;
} client_window_entry_t;

static client_window_entry_t* client_window_map[CLIENT_WINDOW_MAP_SIZE];

static uint32_t
client_window_hash(xcb_window_t window)
{
  return ((uint32_t) (window ^ (window >> 16))) % CLIENT_WINDOW_MAP_SIZE;
}

static void
client_window_map_insert(Client* client)
{
  uint32_t                   hash = client_window_hash(client->window);
  client_window_entry_t*     entry = malloc(sizeof(client_window_entry_t));
  if (entry == NULL) {
    LOG_ERROR("Failed to allocate client window map entry");
    return;
  }
  entry->window           = client->window;
  entry->client           = client;
  entry->next             = client_window_map[hash];
  client_window_map[hash] = entry;
}

static void
client_window_map_remove(xcb_window_t window)
{
  uint32_t               hash = client_window_hash(window);
  client_window_entry_t** prev = &client_window_map[hash];
  client_window_entry_t*  curr = *prev;

  while (curr != NULL) {
    if (curr->window == window) {
      *prev = curr->next;
      free(curr);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }
}

Client*
client_get_by_window(xcb_window_t window)
{
  uint32_t               hash = client_window_hash(window);
  client_window_entry_t* curr = client_window_map[hash];

  while (curr != NULL) {
    if (curr->window == window) {
      return curr->client;
    }
    curr = curr->next;
  }
  return NULL;
}

Client*
client_create(xcb_window_t window)
{
  if (window == XCB_WINDOW_NONE) {
    LOG_ERROR("Cannot create client for XCB_WINDOW_NONE");
    return NULL;
  }

  /* Check if client already exists for this window */
  if (client_get_by_window(window) != NULL) {
    LOG_ERROR("Client already exists for window %u", window);
    return NULL;
  }

  Client* client = calloc(1, sizeof(Client));
  if (client == NULL) {
    LOG_ERROR("Failed to allocate client");
    return NULL;
  }

  /* Initialize client */
  client->window        = window;
  client->name[0]       = '\0';
  client->class_name[0] = '\0';
  client->x            = 0;
  client->y            = 0;
  client->width        = 0;
  client->height       = 0;
  client->border_width = 0;
  client->flags        = CLIENT_FLAG_MANAGED;
  client->tags         = 0;
  client->next         = NULL;
  client->prev         = NULL;
  client->components   = NULL;

  /* Initialize HubTarget */
  client->base.id         = (TargetID) window;
  client->base.type       = TARGET_TYPE_CLIENT;
  client->base.registered = false;

  LOG_DEBUG("Created client for window %u", window);
  return client;
}

void
client_destroy(Client* client)
{
  if (client == NULL) {
    return;
  }

  /* Unregister from hub if registered */
  if (client->base.registered) {
    client_unregister(client);
  }

  /* Remove from window map */
  client_window_map_remove(client->window);

  /* Remove from list */
  if (client->prev != NULL) {
    client->prev->next = client->next;
  }
  if (client->next != NULL) {
    client->next->prev = client->prev;
  }

  /* Free components */
  while (client->components != NULL) {
    ClientComponent* comp = client->components;
    client->components = comp->next;
    free(comp);
  }

  /* Free client */
  LOG_DEBUG("Destroyed client for window %u", client->window);
  free(client);
}

void
client_register(Client* client)
{
  if (client == NULL) {
    LOG_ERROR("Cannot register NULL client");
    return;
  }

  if (client->base.registered) {
    LOG_WARN("Client for window %u is already registered", client->window);
    return;
  }

  /* Add to window map */
  client_window_map_insert(client);

  /* Add to circular list */
  client->prev           = clients_sentinel.prev;
  client->next           = &clients_sentinel;
  clients_sentinel.prev->next = client;
  clients_sentinel.prev   = client;

  /* Register with hub */
  hub_register_target(&client->base);

  clients_count++;

  LOG_DEBUG("Registered client for window %u (total: %u)", client->window, clients_count);
}

void
client_unregister(Client* client)
{
  if (client == NULL) {
    return;
  }

  if (!client->base.registered) {
    LOG_WARN("Client for window %u is not registered", client->window);
    return;
  }

  /* Unregister from hub */
  hub_unregister_target(client->base.id);

  /* Remove from list */
  if (client->prev != NULL) {
    client->prev->next = client->next;
  }
  if (client->next != NULL) {
    client->next->prev = client->prev;
  }

  /* Remove from window map */
  client_window_map_remove(client->window);

  clients_count--;

  LOG_DEBUG("Unregistered client for window %u (remaining: %u)", client->window, clients_count);
}

void
client_set_geometry(Client* client, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
  if (client == NULL) {
    return;
  }

  client->x      = x;
  client->y      = y;
  client->width  = width;
  client->height = height;

  LOG_DEBUG("Client %u geometry: %dx%d at %d,%d", client->window, width, height, x, y);

  /* Emit geometry changed event */
  hub_emit(EVT_CLIENT_GEOMETRY, client->base.id, NULL);
}

void
client_set_name(Client* client, const char* name)
{
  if (client == NULL || name == NULL) {
    return;
  }

  strncpy(client->name, name, CLIENT_NAME_MAX - 1);
  client->name[CLIENT_NAME_MAX - 1] = '\0';

  LOG_DEBUG("Client %u name: %s", client->window, client->name);

  /* Emit name changed event */
  hub_emit(EVT_CLIENT_NAME, client->base.id, NULL);
}

void
client_set_class(Client* client, const char* class_name)
{
  if (client == NULL || class_name == NULL) {
    return;
  }

  strncpy(client->class_name, class_name, CLIENT_NAME_MAX - 1);
  client->class_name[CLIENT_NAME_MAX - 1] = '\0';
}

void
client_set_tags(Client* client, uint32_t tags)
{
  if (client == NULL) {
    return;
  }

  uint32_t old_tags = client->tags;
  client->tags       = tags;

  if (old_tags != tags) {
    LOG_DEBUG("Client %u tags: 0x%x -> 0x%x", client->window, old_tags, tags);
    hub_emit(EVT_CLIENT_TAGS, client->base.id, NULL);
  }
}

void
client_add_tag(Client* client, uint32_t tag)
{
  if (client == NULL) {
    return;
  }

  client_set_tags(client, client->tags | tag);
}

void
client_remove_tag(Client* client, uint32_t tag)
{
  if (client == NULL) {
    return;
  }

  client_set_tags(client, client->tags & ~tag);
}

bool
client_has_tag(Client* client, uint32_t tag)
{
  if (client == NULL) {
    return false;
  }

  return (client->tags & tag) != 0;
}

void
client_set_flags(Client* client, ClientFlags flags)
{
  if (client == NULL) {
    return;
  }

  client->flags |= flags;

  /* Emit events based on flag changes */
  if (flags & CLIENT_FLAG_URGENT) {
    hub_emit(EVT_CLIENT_URGENT, client->base.id, NULL);
  }
  if (flags & CLIENT_FLAG_FULLSCREEN) {
    hub_emit(EVT_CLIENT_FULLSCREEN_ENTERED, client->base.id, NULL);
  }
  if (flags & CLIENT_FLAG_FLOATING) {
    hub_emit(EVT_CLIENT_FLOATING, client->base.id, NULL);
  }
}

void
client_clear_flags(Client* client, ClientFlags flags)
{
  if (client == NULL) {
    return;
  }

  uint32_t old_urgent = client->flags & CLIENT_FLAG_URGENT;
  uint32_t old_fullscreen = client->flags & CLIENT_FLAG_FULLSCREEN;
  uint32_t old_floating = client->flags & CLIENT_FLAG_FLOATING;

  client->flags &= ~flags;

  /* Emit events based on flag changes */
  if ((old_urgent) && !(client->flags & CLIENT_FLAG_URGENT)) {
    hub_emit(EVT_CLIENT_CALM, client->base.id, NULL);
  }
  if ((old_fullscreen) && !(client->flags & CLIENT_FLAG_FULLSCREEN)) {
    hub_emit(EVT_CLIENT_FULLSCREEN_EXITED, client->base.id, NULL);
  }
  if ((old_floating) && !(client->flags & CLIENT_FLAG_FLOATING)) {
    hub_emit(EVT_CLIENT_TILED, client->base.id, NULL);
  }
}

bool
client_has_flags(Client* client, ClientFlags flags)
{
  if (client == NULL) {
    return false;
  }

  return (client->flags & flags) == flags;
}

void
client_adopt(Client* client, ClientComponent* component)
{
  if (client == NULL || component == NULL) {
    return;
  }

  /* Check if already adopted */
  ClientComponent* existing = client_get_component(client, component->name);
  if (existing != NULL) {
    LOG_WARN("Client already has component '%s'", component->name);
    return;
  }

  /* Add to component list */
  component->next     = client->components;
  client->components  = component;

  LOG_DEBUG("Client %u adopted component '%s'", client->window, component->name);
}

void
client_drop(Client* client, const char* component_name)
{
  if (client == NULL || component_name == NULL) {
    return;
  }

  ClientComponent** prev = &client->components;
  ClientComponent*  curr = client->components;

  while (curr != NULL) {
    if (strcmp(curr->name, component_name) == 0) {
      *prev = curr->next;
      free(curr);
      LOG_DEBUG("Client %u dropped component '%s'", client->window, component_name);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }

  LOG_WARN("Client does not have component '%s' to drop", component_name);
}

ClientComponent*
client_get_component(Client* client, const char* name)
{
  if (client == NULL || name == NULL) {
    return NULL;
  }

  ClientComponent* curr = client->components;
  while (curr != NULL) {
    if (strcmp(curr->name, name) == 0) {
      return curr;
    }
    curr = curr->next;
  }

  return NULL;
}

Client*
client_next(Client* client)
{
  if (client == NULL) {
    return NULL;
  }

  Client* next = client->next;
  if (next == &clients_sentinel) {
    return clients_sentinel.next;
  }
  return next;
}

Client*
client_prev(Client* client)
{
  if (client == NULL) {
    return NULL;
  }

  Client* prev = client->prev;
  if (prev == &clients_sentinel) {
    return clients_sentinel.prev;
  }
  return prev;
}

Client*
client_list_head(void)
{
  if (clients_sentinel.next == &clients_sentinel) {
    return NULL;
  }
  return clients_sentinel.next;
}

uint32_t
client_count(void)
{
  return clients_count;
}
