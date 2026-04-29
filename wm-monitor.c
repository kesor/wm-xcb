#include "wm-monitor.h"

#include <stdlib.h>
#include <string.h>

#include "wm-log.h"
#include "wm-client.h"

/* Layout names for display */
const char* layout_names[LAYOUT_COUNT] = {
  [LAYOUT_TILE]     = "tile",
  [LAYOUT_MONOCLE]  = "mono",
  [LAYOUT_FLOATING] = "float",
};

/* Monitor list (sentinel-based circular doubly-linked list) */
static Monitor monitors_sentinel = {
  .next = &monitors_sentinel,
  .prev = &monitors_sentinel,
};

/* Monitor count */
static uint32_t monitors_count = 0;

/* Selected monitor */
static Monitor* selected_monitor = NULL;

/* Hash map for output -> monitor lookup */
#define MONITOR_OUTPUT_MAP_SIZE 128

typedef struct monitor_output_entry {
  xcb_randr_output_t    output;
  Monitor*              monitor;
  struct monitor_output_entry* next;
} monitor_output_entry_t;

static monitor_output_entry_t* monitor_output_map[MONITOR_OUTPUT_MAP_SIZE];

/* Hash map for crtc -> monitor lookup */
#define MONITOR_CRTC_MAP_SIZE 128

typedef struct monitor_crtc_entry {
  xcb_randr_crtc_t    crtc;
  Monitor*            monitor;
  struct monitor_crtc_entry* next;
} monitor_crtc_entry_t;

static monitor_crtc_entry_t* monitor_crtc_map[MONITOR_CRTC_MAP_SIZE];

static uint32_t
monitor_output_hash(xcb_randr_output_t output)
{
  return ((uint32_t) (output ^ (output >> 16))) % MONITOR_OUTPUT_MAP_SIZE;
}

static uint32_t
monitor_crtc_hash(xcb_randr_crtc_t crtc)
{
  return ((uint32_t) (crtc ^ (crtc >> 16))) % MONITOR_CRTC_MAP_SIZE;
}

static void
monitor_output_map_insert(Monitor* monitor)
{
  uint32_t                  hash = monitor_output_hash(monitor->output);
  monitor_output_entry_t*   entry = malloc(sizeof(monitor_output_entry_t));
  if (entry == NULL) {
    LOG_ERROR("Failed to allocate monitor output map entry");
    return;
  }
  entry->output           = monitor->output;
  entry->monitor          = monitor;
  entry->next             = monitor_output_map[hash];
  monitor_output_map[hash] = entry;
}

static void
monitor_output_map_remove(xcb_randr_output_t output)
{
  uint32_t                  hash = monitor_output_hash(output);
  monitor_output_entry_t**  prev = &monitor_output_map[hash];
  monitor_output_entry_t*   curr = *prev;

  while (curr != NULL) {
    if (curr->output == output) {
      *prev = curr->next;
      free(curr);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }
}

static void
monitor_crtc_map_insert(Monitor* monitor)
{
  if (monitor->crtc == 0) {
    return;
  }

  uint32_t                hash = monitor_crtc_hash(monitor->crtc);
  monitor_crtc_entry_t*  entry = malloc(sizeof(monitor_crtc_entry_t));
  if (entry == NULL) {
    LOG_ERROR("Failed to allocate monitor crtc map entry");
    return;
  }
  entry->crtc           = monitor->crtc;
  entry->monitor        = monitor;
  entry->next           = monitor_crtc_map[hash];
  monitor_crtc_map[hash] = entry;
}

static void
monitor_crtc_map_remove(xcb_randr_crtc_t crtc)
{
  uint32_t                hash = monitor_crtc_hash(crtc);
  monitor_crtc_entry_t**  prev = &monitor_crtc_map[hash];
  monitor_crtc_entry_t*  curr = *prev;

  while (curr != NULL) {
    if (curr->crtc == crtc) {
      *prev = curr->next;
      free(curr);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }
}

Monitor*
monitor_get_by_output(xcb_randr_output_t output)
{
  uint32_t                  hash = monitor_output_hash(output);
  monitor_output_entry_t*   curr = monitor_output_map[hash];

  while (curr != NULL) {
    if (curr->output == output) {
      return curr->monitor;
    }
    curr = curr->next;
  }
  return NULL;
}

Monitor*
monitor_get_by_crtc(xcb_randr_crtc_t crtc)
{
  if (crtc == 0) {
    return NULL;
  }

  uint32_t                hash = monitor_crtc_hash(crtc);
  monitor_crtc_entry_t*   curr = monitor_crtc_map[hash];

  while (curr != NULL) {
    if (curr->crtc == crtc) {
      return curr->monitor;
    }
    curr = curr->next;
  }
  return NULL;
}

Monitor*
monitor_create(xcb_randr_output_t output)
{
  if (output == XCB_NONE) {
    LOG_ERROR("Cannot create monitor for XCB_NONE output");
    return NULL;
  }

  /* Check if monitor already exists for this output */
  if (monitor_get_by_output(output) != NULL) {
    LOG_ERROR("Monitor already exists for output %u", output);
    return NULL;
  }

  Monitor* monitor = calloc(1, sizeof(Monitor));
  if (monitor == NULL) {
    LOG_ERROR("Failed to allocate monitor");
    return NULL;
  }

  /* Initialize monitor */
  monitor->output       = output;
  monitor->crtc         = 0;
  monitor->x           = 0;
  monitor->y           = 0;
  monitor->width       = 0;
  monitor->height      = 0;
  monitor->mwidth      = 0;
  monitor->mheight     = 0;
  monitor->mfact       = 0.5f;
  monitor->nmaster     = 1;
  monitor->tagset      = 1;  /* Start on tag 1 */
  monitor->state       = MONITOR_STATE_ACTIVE;
  monitor->layout       = LAYOUT_TILE;
  monitor->clients      = NULL;
  monitor->sel          = NULL;
  monitor->stack        = NULL;
  monitor->next         = NULL;
  monitor->prev         = NULL;
  monitor->components    = NULL;

  /* Initialize HubTarget - use output as the ID */
  monitor->base.id         = (TargetID) output;
  monitor->base.type       = TARGET_TYPE_MONITOR;
  monitor->base.registered = false;

  LOG_DEBUG("Created monitor for output %u", output);
  return monitor;
}

void
monitor_destroy(Monitor* monitor)
{
  if (monitor == NULL) {
    return;
  }

  /* Unregister from hub if registered */
  if (monitor->base.registered) {
    monitor_unregister(monitor);
  }

  /* Remove from maps */
  monitor_output_map_remove(monitor->output);
  monitor_crtc_map_remove(monitor->crtc);

  /* Remove from list */
  if (monitor->prev != NULL) {
    monitor->prev->next = monitor->next;
  }
  if (monitor->next != NULL) {
    monitor->next->prev = monitor->prev;
  }

  /* Free components */
  while (monitor->components != NULL) {
    MonitorComponent* comp = monitor->components;
    monitor->components = comp->next;
    free(comp);
  }

  /* If this was the selected monitor, select another */
  if (selected_monitor == monitor) {
    selected_monitor = monitor_list_head();
    if (selected_monitor == monitor) {
      selected_monitor = NULL;
    }
  }

  /* Free monitor */
  LOG_DEBUG("Destroyed monitor for output %u", monitor->output);
  free(monitor);
}

void
monitor_register(Monitor* monitor)
{
  if (monitor == NULL) {
    LOG_ERROR("Cannot register NULL monitor");
    return;
  }

  if (monitor->base.registered) {
    LOG_WARN("Monitor for output %u is already registered", monitor->output);
    return;
  }

  /* Add to maps */
  monitor_output_map_insert(monitor);
  monitor_crtc_map_insert(monitor);

  /* Add to circular list */
  monitor->prev           = monitors_sentinel.prev;
  monitor->next           = &monitors_sentinel;
  monitors_sentinel.prev->next = monitor;
  monitors_sentinel.prev   = monitor;

  /* Register with hub */
  hub_register_target(&monitor->base);

  monitors_count++;

  /* If this is the first monitor, select it */
  if (selected_monitor == NULL) {
    selected_monitor = monitor;
  }

  LOG_DEBUG("Registered monitor for output %u (total: %u)", monitor->output, monitors_count);
}

void
monitor_unregister(Monitor* monitor)
{
  if (monitor == NULL) {
    return;
  }

  if (!monitor->base.registered) {
    LOG_WARN("Monitor for output %u is not registered", monitor->output);
    return;
  }

  /* Unregister from hub */
  hub_unregister_target(monitor->base.id);

  /* Remove from maps */
  monitor_output_map_remove(monitor->output);
  monitor_crtc_map_remove(monitor->crtc);

  /* Remove from list */
  if (monitor->prev != NULL) {
    monitor->prev->next = monitor->next;
  }
  if (monitor->next != NULL) {
    monitor->next->prev = monitor->prev;
  }

  /* If this was the selected monitor, select another */
  if (selected_monitor == monitor) {
    selected_monitor = monitor_list_head();
    if (selected_monitor == monitor) {
      selected_monitor = NULL;
    }
  }

  monitors_count--;

  LOG_DEBUG("Unregistered monitor for output %u (remaining: %u)", monitor->output, monitors_count);
}

void
monitor_set_geometry(Monitor* monitor, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
  if (monitor == NULL) {
    return;
  }

  bool changed = (monitor->x != x) || (monitor->y != y) ||
                 (monitor->width != width) || (monitor->height != height);

  monitor->x      = x;
  monitor->y      = y;
  monitor->width  = width;
  monitor->height = height;

  if (changed) {
    LOG_DEBUG("Monitor %u geometry: %ux%u at %d,%d", monitor->output, width, height, x, y);
    hub_emit(EVT_MONITOR_GEOMETRY, monitor->base.id, NULL);
  }
}

void
monitor_set_physical(Monitor* monitor, uint16_t width_mm, uint16_t height_mm)
{
  if (monitor == NULL) {
    return;
  }

  monitor->mwidth  = width_mm;
  monitor->mheight = height_mm;
}

void
monitor_set_randr(Monitor* monitor, xcb_randr_output_t output, xcb_randr_crtc_t crtc)
{
  if (monitor == NULL) {
    return;
  }

  /* Update maps if IDs changed */
  if (monitor->output != output) {
    monitor_output_map_remove(monitor->output);
    monitor->output = output;
    monitor_output_map_insert(monitor);
    /* Update HubTarget ID */
    monitor->base.id = (TargetID) output;
  }

  if (monitor->crtc != crtc) {
    monitor_crtc_map_remove(monitor->crtc);
    monitor->crtc = crtc;
    monitor_crtc_map_insert(monitor);
  }

  LOG_DEBUG("Monitor %u RandR: output=%u crtc=%u", monitor->output, output, crtc);
}

void
monitor_set_mfact(Monitor* monitor, float mfact)
{
  if (monitor == NULL) {
    return;
  }

  /* Clamp to valid range */
  if (mfact < 0.0f) mfact = 0.0f;
  if (mfact > 1.0f) mfact = 1.0f;

  monitor->mfact = mfact;
}

void
monitor_set_nmaster(Monitor* monitor, int nmaster)
{
  if (monitor == NULL) {
    return;
  }

  if (nmaster < 0) nmaster = 0;

  monitor->nmaster = nmaster;
}

void
monitor_set_tagset(Monitor* monitor, uint32_t tagset)
{
  if (monitor == NULL) {
    return;
  }

  uint32_t old_tagset = monitor->tagset;
  monitor->tagset     = tagset;

  if (old_tagset != tagset) {
    LOG_DEBUG("Monitor %u tagset: 0x%x -> 0x%x", monitor->output, old_tagset, tagset);
    hub_emit(EVT_MONITOR_TAG_CHANGED, monitor->base.id, NULL);
  }
}

void
monitor_add_tag(Monitor* monitor, uint32_t tag)
{
  if (monitor == NULL) {
    return;
  }

  monitor_set_tagset(monitor, monitor->tagset | tag);
}

void
monitor_remove_tag(Monitor* monitor, uint32_t tag)
{
  if (monitor == NULL) {
    return;
  }

  monitor_set_tagset(monitor, monitor->tagset & ~tag);
}

bool
monitor_has_tag(Monitor* monitor, uint32_t tag)
{
  if (monitor == NULL) {
    return false;
  }

  return (monitor->tagset & tag) != 0;
}

void
monitor_set_layout(Monitor* monitor, uint32_t layout)
{
  if (monitor == NULL) {
    return;
  }

  if (layout >= LAYOUT_COUNT) {
    LOG_WARN("Invalid layout %u", layout);
    return;
  }

  uint32_t old_layout = monitor->layout;
  monitor->layout     = layout;

  if (old_layout != layout) {
    LOG_DEBUG("Monitor %u layout: %u -> %u (%s)", monitor->output, old_layout, layout, layout_names[layout]);
    hub_emit(EVT_MONITOR_LAYOUT_CHANGED, monitor->base.id, NULL);
  }
}

void
monitor_set_state(Monitor* monitor, uint32_t state)
{
  if (monitor == NULL) {
    return;
  }

  monitor->state = state;
}

void
monitor_adopt(Monitor* monitor, MonitorComponent* component)
{
  if (monitor == NULL || component == NULL) {
    return;
  }

  /* Check if already adopted */
  MonitorComponent* existing = monitor_get_component(monitor, component->name);
  if (existing != NULL) {
    LOG_WARN("Monitor already has component '%s'", component->name);
    return;
  }

  /* Add to component list */
  component->next     = monitor->components;
  monitor->components = component;

  LOG_DEBUG("Monitor %u adopted component '%s'", monitor->output, component->name);
}

void
monitor_drop(Monitor* monitor, const char* component_name)
{
  if (monitor == NULL || component_name == NULL) {
    return;
  }

  MonitorComponent** prev = &monitor->components;
  MonitorComponent*  curr = monitor->components;

  while (curr != NULL) {
    if (strcmp(curr->name, component_name) == 0) {
      *prev = curr->next;
      free(curr);
      LOG_DEBUG("Monitor %u dropped component '%s'", monitor->output, component_name);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }

  LOG_WARN("Monitor does not have component '%s' to drop", component_name);
}

MonitorComponent*
monitor_get_component(Monitor* monitor, const char* name)
{
  if (monitor == NULL || name == NULL) {
    return NULL;
  }

  MonitorComponent* curr = monitor->components;
  while (curr != NULL) {
    if (strcmp(curr->name, name) == 0) {
      return curr;
    }
    curr = curr->next;
  }

  return NULL;
}

void
monitor_attach_client(Monitor* monitor, Client* client)
{
  if (monitor == NULL || client == NULL) {
    return;
  }

  /* Add to monitor's client list */
  client->next      = monitor->clients;
  monitor->clients  = client;

  LOG_DEBUG("Attached client %u to monitor %u", client->window, monitor->output);
}

void
monitor_detach_client(Monitor* monitor, Client* client)
{
  if (monitor == NULL || client == NULL) {
    return;
  }

  /* Remove from client list */
  Client** prev = &monitor->clients;
  Client*  curr = monitor->clients;

  while (curr != NULL) {
    if (curr == client) {
      *prev = curr->next;
      LOG_DEBUG("Detached client %u from monitor %u", client->window, monitor->output);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }
}

void
monitor_focus_client(Monitor* monitor, Client* client)
{
  if (monitor == NULL) {
    return;
  }

  if (monitor->sel != client) {
    /* Emit unfocused event for old client */
    if (monitor->sel != NULL) {
      hub_emit(EVT_CLIENT_UNFOCUSED, monitor->sel->base.id, NULL);
    }

    monitor->sel = client;

    /* Emit focused event for new client */
    if (client != NULL) {
      hub_emit(EVT_CLIENT_FOCUSED, client->base.id, NULL);
    }
  }
}

Monitor*
monitor_at(int16_t x, int16_t y)
{
  Monitor* curr = monitors_sentinel.next;
  while (curr != &monitors_sentinel) {
    if (x >= curr->x && x < curr->x + (int16_t) curr->width &&
        y >= curr->y && y < curr->y + (int16_t) curr->height) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

Monitor*
monitor_next(Monitor* monitor)
{
  if (monitor == NULL) {
    return NULL;
  }

  Monitor* next = monitor->next;
  if (next == &monitors_sentinel) {
    return monitors_sentinel.next;
  }
  return next;
}

Monitor*
monitor_prev(Monitor* monitor)
{
  if (monitor == NULL) {
    return NULL;
  }

  Monitor* prev = monitor->prev;
  if (prev == &monitors_sentinel) {
    return monitors_sentinel.prev;
  }
  return prev;
}

Monitor*
monitor_list_head(void)
{
  if (monitors_sentinel.next == &monitors_sentinel) {
    return NULL;
  }
  return monitors_sentinel.next;
}

uint32_t
monitor_count(void)
{
  return monitors_count;
}

Monitor*
monitor_selected(void)
{
  return selected_monitor;
}

void
monitor_select(Monitor* monitor)
{
  Monitor* old = selected_monitor;
  selected_monitor = monitor;

  if (old != monitor) {
    if (old != NULL) {
      hub_emit(EVT_MONITOR_UNFOCUSED, old->base.id, NULL);
    }
    if (monitor != NULL) {
      hub_emit(EVT_MONITOR_FOCUSED, monitor->base.id, NULL);
    }
  }
}
