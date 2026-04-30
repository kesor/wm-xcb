/*
 * Tiling Component Implementation
 *
 * Handles window tiling using a tiled layout algorithm.
 * Manages the LayoutSM state machine and XCB window positioning.
 *
 * The component provides:
 * - LayoutSM template: TILE ↔ MONOCLE ↔ FLOATING
 * - Executor that handles REQ_MONITOR_TILE requests
 * - Tiling algorithm that divides monitor into master and stack
 * - XCB ConfigureRequest to position windows
 */

#include <stdlib.h>
#include <string.h>

#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/xcb/xcb-handler.h"
#include "tiling.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-xcb.h"

/*
 * Cached LayoutSM template - created once at init, reused for all monitors.
 */
static SMTemplate* cached_layout_template = NULL;

/*
 * Global component instance
 */
static TilingComponent tiling_component = {
  .base = {
           .name       = TILING_COMPONENT_NAME,
           .requests   = (RequestType[]) { REQ_MONITOR_TILE, 0 },
           .targets    = (TargetType[]) { TARGET_TYPE_MONITOR, TARGET_TYPE_NONE },
           .executor   = NULL,
           .registered = false,
           },
  .initialized     = false,
  .default_mfact   = 0.5F, /* 50% master, 50% stack */
  .default_nmaster = 1,
};

/*
 * Track if static initialization has occurred
 */
static bool static_init_done = false;

static void
do_static_init(void)
{
  if (static_init_done)
    return;
  static_init_done                 = true;
  tiling_component.initialized     = false;
  tiling_component.base.registered = false;
}

__attribute__((constructor)) static void
ensure_static_init(void)
{
  do_static_init();
}

/*
 * Internal helper to reset component state
 */
static void
tiling_component_reset(void)
{
  tiling_component.initialized = false;
}

/*
 * Get the tiling component instance
 */
TilingComponent*
tiling_component_get(void)
{
  return &tiling_component;
}

/*
 * Check if component is initialized
 */
bool
tiling_component_is_initialized(void)
{
  return tiling_component.initialized;
}

/*
 * SM Event Emitter
 *
 * This function is passed to sm_create() to handle event emission.
 */
static void
tiling_sm_emit(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) sm;

  Monitor* m = (Monitor*) userdata;
  if (m == NULL)
    return;

  EventType event;
  switch (to_state) {
  case LAYOUT_STATE_TILE:
    event = EVT_LAYOUT_TILE_CHANGED;
    break;
  case LAYOUT_STATE_MONOCLE:
    event = EVT_LAYOUT_MONOCLE_CHANGED;
    break;
  case LAYOUT_STATE_FLOATING:
    event = EVT_LAYOUT_FLOATING_CHANGED;
    break;
  default:
    return;
  }

  hub_emit(event, m->target.id, NULL);
  hub_emit(EVT_LAYOUT_CHANGED, m->target.id, NULL);
}

/*
 * Guard functions
 */

/*
 * Check if we can change layout.
 * Always allows layout changes.
 */
bool
tiling_guard_can_change_layout(StateMachine* sm, void* data)
{
  (void) sm;
  (void) data;
  return true;
}

/*
 * Action functions
 */

/*
 * Action called when layout changes.
 * Tiles all clients on the monitor.
 */
bool
tiling_action_on_layout_change(StateMachine* sm, void* data)
{
  (void) data;

  if (sm == NULL || sm->owner == NULL)
    return false;

  Monitor* m = (Monitor*) sm->owner;

  /* Only tile in TILE state */
  if (sm_get_state(sm) == LAYOUT_STATE_TILE) {
    tiling_tile_monitor(m);
  }

  return true;
}

/*
 * State Machine Template
 */
SMTemplate*
layout_sm_template_create(void)
{
  /* Define states */
  static uint32_t states[] = {
    LAYOUT_STATE_TILE,
    LAYOUT_STATE_MONOCLE,
    LAYOUT_STATE_FLOATING,
  };

  /* Define transitions:
   * TILE ↔ MONOCLE
   * TILE ↔ FLOATING
   * MONOCLE ↔ FLOATING
   */
  static SMTransition transitions[] = {
    /* TILE → MONOCLE */
    {
     .from_state = LAYOUT_STATE_TILE,
     .to_state   = LAYOUT_STATE_MONOCLE,
     .guard_fn   = "tiling_guard_can_change_layout",
     .action_fn  = "tiling_action_on_layout_change",
     .emit_event = EVT_LAYOUT_MONOCLE_CHANGED,
     },
    /* MONOCLE → TILE */
    {
     .from_state = LAYOUT_STATE_MONOCLE,
     .to_state   = LAYOUT_STATE_TILE,
     .guard_fn   = "tiling_guard_can_change_layout",
     .action_fn  = "tiling_action_on_layout_change",
     .emit_event = EVT_LAYOUT_TILE_CHANGED,
     },
    /* TILE → FLOATING */
    {
     .from_state = LAYOUT_STATE_TILE,
     .to_state   = LAYOUT_STATE_FLOATING,
     .guard_fn   = "tiling_guard_can_change_layout",
     .action_fn  = "tiling_action_on_layout_change",
     .emit_event = EVT_LAYOUT_FLOATING_CHANGED,
     },
    /* FLOATING → TILE */
    {
     .from_state = LAYOUT_STATE_FLOATING,
     .to_state   = LAYOUT_STATE_TILE,
     .guard_fn   = "tiling_guard_can_change_layout",
     .action_fn  = "tiling_action_on_layout_change",
     .emit_event = EVT_LAYOUT_TILE_CHANGED,
     },
    /* MONOCLE → FLOATING */
    {
     .from_state = LAYOUT_STATE_MONOCLE,
     .to_state   = LAYOUT_STATE_FLOATING,
     .guard_fn   = "tiling_guard_can_change_layout",
     .action_fn  = "tiling_action_on_layout_change",
     .emit_event = EVT_LAYOUT_FLOATING_CHANGED,
     },
    /* FLOATING → MONOCLE */
    {
     .from_state = LAYOUT_STATE_FLOATING,
     .to_state   = LAYOUT_STATE_MONOCLE,
     .guard_fn   = "tiling_guard_can_change_layout",
     .action_fn  = "tiling_action_on_layout_change",
     .emit_event = EVT_LAYOUT_MONOCLE_CHANGED,
     },
  };

  SMTemplate* tmpl = sm_template_create(
      "layout",
      states,
      3,
      transitions,
      6,
      LAYOUT_STATE_TILE);

  if (tmpl == NULL) {
    LOG_ERROR("Failed to create layout state machine template");
    return NULL;
  }

  return tmpl;
}

/*
 * State Machine Accessors
 */

/*
 * Get or create the layout state machine for a monitor.
 */
StateMachine*
tiling_get_sm(Monitor* m)
{
  if (m == NULL)
    return NULL;

  StateMachine* sm = monitor_get_sm(m, TILING_COMPONENT_NAME);
  if (sm != NULL)
    return sm;

  /* Use cached template if available, otherwise create one */
  SMTemplate* tmpl = cached_layout_template;
  if (tmpl == NULL) {
    tmpl = layout_sm_template_create();
    if (tmpl == NULL) {
      LOG_ERROR("Failed to create layout SM template for monitor");
      return NULL;
    }
  }

  sm = sm_create(m, tmpl, tiling_sm_emit, m);
  if (sm == NULL) {
    LOG_ERROR("Failed to create layout SM instance for monitor");
    return NULL;
  }

  /* Store in monitor */
  if (!monitor_set_sm(m, TILING_COMPONENT_NAME, sm)) {
    LOG_ERROR("Failed to store layout SM for monitor");
    sm_destroy(sm);
    return NULL;
  }

  LOG_DEBUG("Created layout SM for monitor output=%u", m->output);
  return sm;
}

/*
 * Get current layout state for a monitor
 */
LayoutState
tiling_get_state(Monitor* m)
{
  if (m == NULL)
    return LAYOUT_STATE_TILE;

  StateMachine* sm = monitor_get_sm(m, TILING_COMPONENT_NAME);
  if (sm == NULL)
    return LAYOUT_STATE_TILE;

  return (LayoutState) sm_get_state(sm);
}

/*
 * Set layout state directly (for raw_write from listeners).
 */
void
tiling_set_state(Monitor* m, LayoutState state)
{
  if (m == NULL)
    return;

  StateMachine* sm = tiling_get_sm(m);
  if (sm == NULL) {
    LOG_WARN("Cannot set layout state: failed to get/create SM");
    return;
  }

  sm_raw_write(sm, state);
}

/*
 * Tiling Functions
 */

/*
 * Calculate tiled geometry for master area.
 */
void
tiling_master_geometry(
    Monitor*  m,
    int       nmaster,
    float     mfact,
    int16_t*  x,
    int16_t*  y,
    uint16_t* w,
    uint16_t* h)
{
  if (m == NULL)
    return;

  /* Master area takes mfact of monitor width */
  *x = m->x;
  *y = m->y;

  if (nmaster > 0) {
    *w = (uint16_t) ((float) m->width * mfact);
  } else {
    *w = m->width;
  }

  *h = m->height;
}

/*
 * Calculate tiled geometry for stack area.
 */
void
tiling_stack_geometry(
    Monitor*  m,
    int       nmaster,
    float     mfact,
    int16_t*  x,
    int16_t*  y,
    uint16_t* w,
    uint16_t* h)
{
  if (m == NULL)
    return;

  /* Stack area is the remaining width */
  *x = (int16_t) ((int32_t) m->x + (int32_t) ((float) m->width * mfact));
  *y = m->y;

  if (nmaster > 0) {
    *w = (uint16_t) ((float) m->width * (1.0F - mfact));
  } else {
    *w = 0;
  }

  *h = m->height;
}

/*
 * Get master factor for a monitor.
 * Uses monitor's mfact if set, otherwise default.
 */
static float
tiling_get_mfact(Monitor* m)
{
  (void) m;
  return tiling_component.default_mfact;
}

/*
 * Get nmaster for a monitor.
 * Uses monitor's nmaster if set, otherwise default.
 */
static int
tiling_get_nmaster(Monitor* m)
{
  (void) m;
  return tiling_component.default_nmaster;
}

/*
 * Count managed clients visible on a monitor.
 */
static uint32_t
tiling_count_visible_clients(Monitor* m)
{
  uint32_t count = 0;
  Client*  c     = client_list_sentinel()->next;

  while (c != client_list_sentinel()) {
    if (c->managed && c->monitor == m) {
      count++;
    }
    c = c->next;
  }

  return count;
}

/*
 * Collect visible, managed clients on a monitor.
 * Returns array of clients (caller must free).
 */
static Client**
tiling_collect_clients(Monitor* m, uint32_t* count)
{
  *count = tiling_count_visible_clients(m);
  if (*count == 0)
    return NULL;

  uint32_t capacity = *count;
  Client** clients = malloc(capacity * sizeof(Client*));
  if (clients == NULL)
    return NULL;

  /* Initialize all entries to NULL for safety */
  for (uint32_t init_i = 0; init_i < capacity; init_i++) {
    clients[init_i] = NULL;
  }

  uint32_t idx = 0;
  Client* c     = client_list_sentinel()->next;

  while (c != client_list_sentinel()) {
    if (c->managed && c->monitor == m) {
      if (idx < capacity) {
        clients[idx++] = c;
      } else {
        /* Array full but more clients found - shouldn't happen */
        idx++;
      }
    }
    c = c->next;
  }

  return clients;
}

/*
 * Arrange clients using the tiled layout algorithm.
 */
void
tiling_arrange(
    Monitor* m,
    Client** master_clients,
    int      nmaster,
    Client** stack_clients,
    int      nstack)
{
  if (m == NULL)
    return;

  float mfact = tiling_get_mfact(m);

  /* Calculate master area geometry */
  int16_t  mx, my;
  uint16_t mw, mh;
  tiling_master_geometry(m, nmaster, mfact, &mx, &my, &mw, &mh);

  /* Calculate stack area geometry */
  int16_t  sx, sy;
  uint16_t sw, sh;
  tiling_stack_geometry(m, nmaster, mfact, &sx, &sy, &sw, &sh);

  /* Tile master clients vertically */
  if (nmaster > 0 && mw > 0) {
    uint16_t h = mh / (uint16_t) nmaster;
    for (int i = 0; i < nmaster; i++) {
      Client* c = master_clients[i];
      if (c == NULL)
        continue;

      c->x            = mx;
      c->y            = (int16_t) (my + ((int32_t) i * (int32_t) h));
      c->width        = mw;
      c->height       = h;
      c->border_width = 1;

      /* Configure window via X */
      client_configure_from_struct(c);
    }
  }

  /* Tile stack clients */
  if (nstack > 0) {
    if (sw > 0) {
      /* Arrange stack clients vertically */
      uint16_t h = sh / (uint16_t) nstack;
      for (int i = 0; i < nstack; i++) {
        Client* c = stack_clients[i];
        if (c == NULL)
          continue;

        c->x            = sx;
        c->y            = (int16_t) (sy + ((int32_t) i * (int32_t) h));
        c->width        = sw;
        c->height       = h;
        c->border_width = 1;

        /* Configure window via X */
        client_configure_from_struct(c);
      }
    } else {
      /* No stack area - tile in master */
      uint16_t h      = mh / (uint16_t) (nmaster + nstack);
      uint16_t offset = (uint16_t) nmaster * h;
      for (int i = 0; i < nstack; i++) {
        Client* c = stack_clients[i];
        if (c == NULL)
          continue;

        c->x            = mx;
        c->y            = (int16_t) (my + ((int32_t) offset + (int32_t) i * (int32_t) h));
        c->width        = mw;
        c->height       = h;
        c->border_width = 1;

        /* Configure window via X */
        client_configure_from_struct(c);
      }
    }
  }
}

/*
 * Tile all clients on a monitor according to current layout.
 */
void
tiling_tile_monitor(Monitor* m)
{
  if (m == NULL)
    return;

  LOG_DEBUG("Tiling monitor output=%u", m->output);

  /* Get current layout state */
  StateMachine* sm = tiling_get_sm(m);
  if (sm == NULL) {
    LOG_WARN("No layout SM for monitor, using default tile");
    sm = tiling_get_sm(m);
    if (sm == NULL)
      return;
  }

  LayoutState state = (LayoutState) sm_get_state(sm);

  if (state == LAYOUT_STATE_MONOCLE) {
    /* Monocle: only show the first managed client, full screen */
    Client* sel = NULL;
    Client* c   = client_list_sentinel()->next;
    while (c != client_list_sentinel()) {
      if (c->managed && c->monitor == m) {
        sel = c;
        break;
      }
      c = c->next;
    }

    if (sel != NULL) {
      sel->x            = m->x;
      sel->y            = m->y;
      sel->width        = m->width;
      sel->height       = m->height;
      sel->border_width = 0;
      client_configure_from_struct(sel);

      /* Hide all other clients on this monitor */
      c = client_list_sentinel()->next;
      while (c != client_list_sentinel()) {
        if (c != sel && c->managed && c->monitor == m) {
          client_hide(c->window);
        }
        c = c->next;
      }

      /* Show the selected client */
      client_show(sel->window);
    }
    return;
  }

  if (state == LAYOUT_STATE_FLOATING) {
    /* Floating: don't tile, restore stored positions */
    Client* c = client_list_sentinel()->next;
    while (c != client_list_sentinel()) {
      if (c->managed && c->monitor == m) {
        client_configure_from_struct(c);
      }
      c = c->next;
    }
    return;
  }

  /* TILE state - use tiled layout */
  int nmaster = tiling_get_nmaster(m);

  /* Collect clients */
  uint32_t client_count;
  Client** clients = tiling_collect_clients(m, &client_count);
  if (clients == NULL || client_count == 0) {
    free(clients);
    LOG_DEBUG("No clients to tile on monitor");
    return;
  }

  /* Separate master and stack clients */
  int actual_nmaster = (nmaster > (int) client_count) ? (int) client_count : nmaster;

  /* Restack: master clients drawn first, stack clients drawn on top */
  Client** master_clients = clients;
  Client** stack_clients  = &clients[actual_nmaster];
  int      nstack         = (int) client_count - actual_nmaster;

  /* Arrange clients */
  tiling_arrange(m, master_clients, actual_nmaster, stack_clients, nstack);

  /* Show all clients */
  for (uint32_t i = 0; i < client_count; i++) {
    Client* c = clients[i];
    if (c != NULL) {
      client_show(c->window);
    }
  }

  free(clients);

  LOG_DEBUG("Tiled %" PRIu32 " clients (nmaster=%d) on monitor", client_count, actual_nmaster);
}

/*
 * Tile a specific client in the master area.
 */
void
tiling_tile_client(Client* c)
{
  if (c == NULL)
    return;

  Monitor* m = c->monitor;
  if (m == NULL)
    return;

  /* Get current layout state */
  LayoutState state = tiling_get_state(m);
  if (state != LAYOUT_STATE_TILE) {
    /* Only tile in TILE state */
    return;
  }

  int   nmaster = tiling_get_nmaster(m);
  float mfact   = tiling_get_mfact(m);

  /* Calculate master area geometry */
  int16_t  mx, my;
  uint16_t mw, mh;
  tiling_master_geometry(m, nmaster, mfact, &mx, &my, &mw, &mh);

  /* Position the client in master area */
  c->x            = mx;
  c->y            = my;
  c->width        = mw;
  c->height       = mh;
  c->border_width = 1;

  /* Configure window via X */
  client_configure_from_struct(c);

  LOG_DEBUG("Tiled client window=%u in master area", c->window);
}

/*
 * Get the default master factor.
 */
float
tiling_get_default_mfact(void)
{
  return tiling_component.default_mfact;
}

/*
 * Get the default number of master clients.
 */
int
tiling_get_default_nmaster(void)
{
  return tiling_component.default_nmaster;
}

/*
 * Executor: Handle REQ_MONITOR_TILE request
 */
static void
tiling_executor(struct HubRequest* req)
{
  if (req == NULL)
    return;

  LOG_DEBUG("tiling_executor: type=%u target=%" PRIu64,
            req->type, req->target);

  /* Get monitor from target ID */
  Monitor* m = (Monitor*) hub_get_target_by_id(req->target);
  if (m == NULL || m->target.type != TARGET_TYPE_MONITOR) {
    LOG_DEBUG("tiling_executor: no monitor found for target");
    return;
  }

  /* Get or create SM */
  StateMachine* sm = tiling_get_sm(m);
  if (sm == NULL) {
    LOG_ERROR("tiling_executor: failed to get layout SM");
    return;
  }

  /* Get current state */
  uint32_t current = sm_get_state(sm);

  /* If data indicates a specific layout, use it; otherwise toggle */
  LayoutState target;
  if (req->data != NULL) {
    target = *(LayoutState*) req->data;
  } else {
    /* Default: toggle between TILE and MONOCLE */
    target = (current == LAYOUT_STATE_TILE)
                 ? LAYOUT_STATE_MONOCLE
                 : LAYOUT_STATE_TILE;
  }

  /* Check if we need to transition */
  if (current != target) {
    /* Transition to new state - this will trigger action which tiles */
    sm_transition(sm, target);
  } else {
    /* Already in target state, just re-tile */
    if (target == LAYOUT_STATE_TILE) {
      tiling_tile_monitor(m);
    }
  }

  LOG_DEBUG("tiling_executor: layout for monitor output=%u is now %u",
            m->output, sm_get_state(sm));
}

/*
 * Component initialization
 */
bool
tiling_component_init(void)
{
  /* Check if already registered in hub */
  HubComponent* existing = hub_get_component_by_name(TILING_COMPONENT_NAME);
  if (existing != NULL) {
    if (tiling_component.initialized) {
      LOG_DEBUG("Tiling component already initialized and registered");
      return true;
    }
    LOG_DEBUG("Component registered with hub, completing initialization");
    tiling_component.initialized = true;
    return true;
  }

  /* Reset if inconsistent state */
  if (tiling_component.initialized) {
    LOG_DEBUG("Tiling component in inconsistent state, resetting");
    tiling_component_reset();
  }

  LOG_DEBUG("Initializing tiling component");

  /* Register guards and actions with SM registry */
  sm_register_guard("tiling_guard_can_change_layout", tiling_guard_can_change_layout);
  sm_register_action("tiling_action_on_layout_change", tiling_action_on_layout_change);

  /* Register executor */
  tiling_component.base.executor = tiling_executor;

  /* Register with hub */
  hub_register_component(&tiling_component.base);

  /* Cache the template for future monitors */
  cached_layout_template = layout_sm_template_create();
  if (cached_layout_template == NULL) {
    LOG_ERROR("Failed to create layout SM template");
    hub_unregister_component(TILING_COMPONENT_NAME);
    tiling_component_reset();
    return false;
  }

  tiling_component.initialized = true;
  LOG_DEBUG("Tiling component initialized successfully");

  return true;
}

/*
 * Component shutdown
 */
void
tiling_component_shutdown(void)
{
  if (!tiling_component.initialized) {
    return;
  }

  LOG_DEBUG("Shutting down tiling component");

  /* Unregister from hub */
  hub_unregister_component(TILING_COMPONENT_NAME);

  /* Unregister guards and actions */
  sm_unregister_guard("tiling_guard_can_change_layout");
  sm_unregister_action("tiling_action_on_layout_change");

  /* Free cached template */
  if (cached_layout_template != NULL) {
    sm_template_destroy(cached_layout_template);
    cached_layout_template = NULL;
  }

  tiling_component.initialized = false;
  LOG_DEBUG("Tiling component shutdown complete");
}
