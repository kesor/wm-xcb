/*
 * Tag Manager Component Implementation
 *
 * Handles tag view state for monitors.
 * - Executor handles REQ_TAG_VIEW, REQ_TAG_TOGGLE, REQ_TAG_CLIENT_TOGGLE, and
 *   answers REQ_CLIENT_VISIBLE, the manage path's question about a window
 * - Uses TagViewSM to track visible tag bitmask
 * - On tag change: shows/hides clients based on tag membership
 * - Emits EVT_TAG_CHANGED event
 * - Sends REQ_MONITOR_TILE after the visible set changes
 */

#include <stdlib.h>
#include <string.h>

#include "client-list.h"
#include "src/actions/action-registry.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-log.h"
#include "src/sm/sm-instance.h"
#include "src/sm/sm-registry.h"
#include "src/sm/sm-template.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "tag-manager.h"

/*
 * Tag Manager SM template name
 */
#define TAG_VIEW_SM_NAME "tag-view"

/*
 * Tag Manager SM states
 */
enum {
  TAG_VIEW_EMPTY  = 0, /* No tags visible */
  TAG_VIEW_TAGGED = 1, /* One or more tags visible */
};

/*
 * Tag Manager SM template
 */
static SMTemplate* cached_tag_view_template = NULL;

/*
 * Internal event tracking for testing
 */
typedef struct {
  bool     tag_changed_received;
  TargetID tag_changed_target;
  uint32_t old_mask;
  uint32_t new_mask;
} TagManagerEvents;

static TagManagerEvents tag_manager_test_events;

/* Forward declarations for component hooks */
void tag_manager_on_adopt(HubTarget* target);
void tag_manager_on_unadopt(HubTarget* target);

/*
 * Global component instance
 */
static TagManagerComponent tag_manager_component_instance = {
  .base = {
           .name     = TAG_MANAGER_COMPONENT_NAME,
           .requests = (RequestType[]) {
          REQ_TAG_VIEW,
          REQ_TAG_TOGGLE,
          REQ_TAG_CLIENT_TOGGLE,
          REQ_CLIENT_VISIBLE,
          0,
      },
           .accepted_target_names = (const char*[]) { "monitor", NULL },
           .accepted_targets      = NULL,
           .executor              = NULL, /* Set below */
      .on_adopt              = tag_manager_on_adopt,
           .on_unadopt            = tag_manager_on_unadopt,
           .registered            = false,
           },
  .initialized = false,
};

static HubComponent*
tag_manager_component(void)
{
  return &tag_manager_component_instance.base;
}

/*
 * Rearrange the monitor after a tag change made a different set of clients
 * visible. Sent as a request: tag-manager does not call the tiling component.
 */
static void
tag_manager_request_tile(Monitor* m)
{
  if (m == NULL)
    return;

  hub_send_request(REQ_MONITOR_TILE, m->target.id);
}

/*
 * Track static initialization
 */
static bool static_init_done = false;

static void
do_static_init(void)
{
  if (static_init_done)
    return;
  static_init_done                               = true;
  tag_manager_component_instance.initialized     = false;
  tag_manager_component_instance.base.registered = false;
  tag_manager_test_events.tag_changed_received   = false;
  tag_manager_test_events.tag_changed_target     = TARGET_ID_NONE;
}

__attribute__((constructor)) static void
ensure_static_init(void)
{
  do_static_init();
}

/*
 * SM Event Emitter
 *
 * Emits EVT_TAG_CHANGED when the tag view state changes.
 */
static void
tag_view_sm_emit(StateMachine* sm, uint32_t from_state, uint32_t to_state, void* userdata)
{
  (void) from_state;

  if (sm == NULL || sm_get_owner(sm) == NULL)
    return;

  Monitor* m = (Monitor*) sm_get_owner(sm);

  /* Get the tag mask from the SM data (stored as void*) */
  uint32_t* tag_mask_ptr = (uint32_t*) sm_get_data(sm);
  uint32_t  new_mask     = tag_mask_ptr ? *tag_mask_ptr : 0;

  LOG_DEBUG("Tag view changed for monitor %lu: mask=0x%x",
            (unsigned long) m->target.id, new_mask);

  /* Track events for testing */
  tag_manager_test_events.tag_changed_received = true;
  tag_manager_test_events.tag_changed_target   = m->target.id;
  tag_manager_test_events.old_mask             = from_state == TAG_VIEW_EMPTY ? 0 : TAG_ALL_TAGS;
  tag_manager_test_events.new_mask             = new_mask;

  /* Emit the event */
  hub_emit(EVT_TAG_CHANGED, m->target.id, &new_mask);

  /* Update visibility for all clients on this monitor */
  tag_manager_update_visibility(m);
  tag_manager_request_tile(m);
}

/*
 * Reset test events (for test isolation)
 */
void
tag_manager_reset_test_events(void)
{
  tag_manager_test_events.tag_changed_received = false;
  tag_manager_test_events.tag_changed_target   = TARGET_ID_NONE;
  tag_manager_test_events.old_mask             = 0;
  tag_manager_test_events.new_mask             = 0;
}

/*
 * Get test events (for testing)
 */
TagManagerEvents*
tag_manager_get_test_events(void)
{
  return &tag_manager_test_events;
}

/*
 * Tag View SM Template Creation
 */
static SMTemplate*
tag_view_sm_template_create(void)
{
  /* Define states */
  static uint32_t states[] = {
    TAG_VIEW_EMPTY,
    TAG_VIEW_TAGGED,
  };

  /* Define transitions:
   * EMPTY → TAGGED (when at least one tag is selected, emit: EVT_TAG_CHANGED)
   * TAGGED → EMPTY (when all tags are deselected, emit: EVT_TAG_CHANGED)
   * TAGGED → TAGGED (when tag selection changes within TAGGED state, emit: EVT_TAG_CHANGED)
   */
  static SMTransition transitions[] = {
    {
     .from_state = TAG_VIEW_EMPTY,
     .to_state   = TAG_VIEW_TAGGED,
     .guard_fn   = NULL,            /* Always allowed */
        .action_fn  = NULL,
     .emit_event = EVT_TAG_CHANGED,
     },
    {
     .from_state = TAG_VIEW_TAGGED,
     .to_state   = TAG_VIEW_EMPTY,
     .guard_fn   = NULL, /* Allowed - can deselect all tags */
        .action_fn  = NULL,
     .emit_event = EVT_TAG_CHANGED,
     },
    {
     .from_state = TAG_VIEW_TAGGED,
     .to_state   = TAG_VIEW_TAGGED,
     .guard_fn   = NULL,                             /* Always allowed - tag switching within same state */
        .action_fn  = NULL,
     .emit_event = EVT_TAG_CHANGED,
     },
  };

  SMTemplate* tmpl = sm_template_create(
      TAG_VIEW_SM_NAME,
      states,
      2,
      transitions,
      3,
      TAG_VIEW_EMPTY);

  if (tmpl == NULL) {
    LOG_ERROR("Failed to create tag-view state machine template");
    return NULL;
  }

  return tmpl;
}

/*
 * Get or create TagViewSM for a monitor
 */
StateMachine*
tag_manager_get_view_sm(Monitor* m)
{
  if (m == NULL)
    return NULL;

  StateMachine* sm = monitor_get_sm(m, TAG_VIEW_SM_NAME);
  if (sm != NULL)
    return sm;

  /* Use cached template if available */
  SMTemplate* tmpl = cached_tag_view_template;
  if (tmpl == NULL) {
    tmpl = tag_view_sm_template_create();
    if (tmpl == NULL) {
      LOG_ERROR("Failed to create tag-view SM template");
      return NULL;
    }
  }

  /* Allocate tag mask storage */
  uint32_t* tag_mask = malloc(sizeof(uint32_t));
  if (tag_mask == NULL) {
    LOG_ERROR("Failed to allocate tag mask for tag-view SM");
    return NULL;
  }
  *tag_mask = TAG_MASK(0); /* Default to tag 1 (index 0) */

  sm = sm_create(m, tmpl, tag_view_sm_emit, m);
  if (sm == NULL) {
    LOG_ERROR("Failed to create tag-view SM instance");
    free(tag_mask);
    return NULL;
  }

  /*
   * The machine owns the mask, so destroying the machine frees it. A monitor
   * destroyed without this component running its unadopt hook would otherwise
   * leak the mask.
   */
  sm_set_owned_data(sm, tag_mask, free);

  /* Store in monitor */
  if (!monitor_set_sm(m, TAG_VIEW_SM_NAME, sm)) {
    LOG_ERROR("Failed to store tag-view SM in monitor");
    sm_destroy(sm);
    return NULL;
  }

  /* Transition to TAGGED state (default shows tag 1) */
  sm_transition(sm, TAG_VIEW_TAGGED);

  LOG_DEBUG("Created tag-view SM for monitor %lu", (unsigned long) m->target.id);
  return sm;
}

/*
 * Get current visible tag mask for a monitor
 */
uint32_t
tag_manager_get_visible_tags(Monitor* m)
{
  if (m == NULL)
    return 0;

  StateMachine* sm = monitor_get_sm(m, TAG_VIEW_SM_NAME);
  if (sm == NULL || sm_get_data(sm) == NULL)
    return 0;

  uint32_t* tag_mask = (uint32_t*) sm_get_data(sm);
  return *tag_mask;
}

/*
 * Set visible tag mask for a monitor
 */
void
tag_manager_set_visible_tags(Monitor* m, uint32_t tag_mask)
{
  if (m == NULL)
    return;

  StateMachine* sm = tag_manager_get_view_sm(m);
  if (sm == NULL)
    return;

  uint32_t* current_mask = (uint32_t*) sm_get_data(sm);
  if (current_mask == NULL)
    return;

  if (*current_mask == tag_mask) {
    /* No change */
    return;
  }

  /* Update the mask */
  *current_mask = tag_mask;

  /* Emit event */
  hub_emit(EVT_TAG_CHANGED, m->target.id, &tag_mask);

  /* Update visibility */
  tag_manager_update_visibility(m);
  tag_manager_request_tile(m);
}

/*
 * Check if a client is visible based on its tags and monitor's tag view
 */
bool
tag_manager_is_client_visible(Monitor* m, Client* c)
{
  if (m == NULL || c == NULL)
    return false;

  uint32_t visible_tags = tag_manager_get_visible_tags(m);
  if (visible_tags == 0)
    return false;

  /* Client is visible if any of its tags overlap with visible tags */
  return (c->tags & visible_tags) != 0;
}

/*
 * Answer REQ_CLIENT_VISIBLE for one client: should its window be on screen.
 *
 * The client list asks this before it maps a window it has just taken over,
 * because a client can carry tags that the view is not on - the view moved
 * after its tags were set, or a component set them. A client with no tags yet
 * is the one the manage event is about to give the view's tags to, so it
 * belongs on screen exactly when that view is not empty.
 *
 * `data` is the caller's bool, holding the answer to use when nobody handles
 * the request. It is left alone for a target that is not a client on a monitor,
 * which is the same as not answering at all.
 */
static void
tag_manager_answer_visible(TargetID target, void* data)
{
  if (data == NULL)
    return;

  HubTarget* t = hub_get_target_by_id(target);
  if (t == NULL || t->type_id != hub_get_target_type_id_by_name("client"))
    return;

  Client*  c = (Client*) t;
  Monitor* m = client_get_monitor(c);
  if (m == NULL)
    return;

  uint32_t visible_tags = tag_manager_get_visible_tags(m);
  bool     visible;

  if (c->tags == 0)
    visible = visible_tags != 0;
  else
    visible = tag_manager_is_client_visible(m, c);

  if (!visible)
    LOG_INFO("client %u stays off screen: tags=0x%x, visible=0x%x",
             c->window, c->tags, visible_tags);

  *(bool*) data = visible;
}

/*
 * Update client visibility based on tag view
 * Shows clients that match visible tags, hides others
 */
void
tag_manager_update_visibility(Monitor* m)
{
  if (m == NULL)
    return;

  uint32_t visible_tags = tag_manager_get_visible_tags(m);
  if (visible_tags == 0) {
    /* No tags visible - hide all clients on this monitor */
    LOG_DEBUG("Tag view empty - hiding all clients on monitor %lu",
              (unsigned long) m->target.id);
    return;
  }

  /* Iterate all clients and update their visibility */
  Client* c = client_list_get_head();
  while (c != NULL) {
    if (c->monitor == m && client_is_managed(c)) {
      bool should_be_visible = (c->tags & visible_tags) != 0;

      if (should_be_visible && !client_is_mapped(c)) {
        /* Show client */
        LOG_DEBUG("Showing client %u (tags=0x%x, visible=0x%x)",
                  c->window, c->tags, visible_tags);
        client_show(c->window);
        client_set_mapped(c, true);
      } else if (!should_be_visible && client_is_mapped(c)) {
        /* Hide client */
        LOG_DEBUG("Hiding client %u (tags=0x%x, visible=0x%x)",
                  c->window, c->tags, visible_tags);
        client_hide(c->window);
        client_set_mapped(c, false);
      }
    }
    c = client_get_next(c);
  }
}

/*
 * Actions
 *
 * The three tag actions the default config wires. Each one turns an invocation
 * into the request this component already handles, so a keybinding and any
 * other caller take the same path.
 */

/*
 * The tag number arrives as a pointer-sized argument, the way
 * keybinding-binding packs KB_TAG() arguments, and is read back in the
 * executor as a uint32_t.
 */
static bool
tag_manager_action_tag(ActionInvocation* inv, uint32_t* tag)
{
  if (inv == NULL || inv->data == NULL)
    return false;

  *tag = (uint32_t) (uintptr_t) inv->data;
  return true;
}

/*
 * The executor runs inside hub_send_request_data(), before it returns, so a
 * tag on this frame is still alive when the handler reads it.
 */
static bool
tag_manager_action_view(ActionInvocation* inv)
{
  uint32_t tag;
  if (inv == NULL || !tag_manager_action_tag(inv, &tag))
    return false;

  hub_send_request_data(REQ_TAG_VIEW, inv->target, &tag);
  return true;
}

static bool
tag_manager_action_toggle(ActionInvocation* inv)
{
  uint32_t tag;
  if (inv == NULL || !tag_manager_action_tag(inv, &tag))
    return false;

  hub_send_request_data(REQ_TAG_TOGGLE, inv->target, &tag);
  return true;
}

static bool
tag_manager_action_client_move(ActionInvocation* inv)
{
  uint32_t tag;
  if (inv == NULL || !tag_manager_action_tag(inv, &tag))
    return false;

  hub_send_request_data(REQ_TAG_CLIENT_TOGGLE, inv->target, &tag);
  return true;
}

static Action tag_manager_view_action = {
  .name            = "tag-manager.view",
  .description     = "Show tag N on the selected monitor",
  .callback        = tag_manager_action_view,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_MONITOR,
  .target_required = true,
  .userdata        = NULL,
};

static Action tag_manager_toggle_action = {
  .name            = "tag-manager.toggle",
  .description     = "Toggle visibility of tag N on the selected monitor",
  .callback        = tag_manager_action_toggle,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_MONITOR,
  .target_required = true,
  .userdata        = NULL,
};

static Action tag_manager_client_move_action = {
  .name            = "tag-manager.client-move",
  .description     = "Move the focused client to tag N",
  .callback        = tag_manager_action_client_move,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_CLIENT,
  .target_required = true,
  .userdata        = NULL,
};

/*
 * Register the tag actions before the component reaches the hub, so a
 * half-initialized component is never visible to anyone.
 */
static bool
tag_manager_register_actions(void)
{
  if (!action_register(&tag_manager_view_action)) {
    LOG_ERROR("Failed to register action '%s'", tag_manager_view_action.name);
    return false;
  }

  if (!action_register(&tag_manager_toggle_action)) {
    LOG_ERROR("Failed to register action '%s'", tag_manager_toggle_action.name);
    action_unregister(tag_manager_view_action.name);
    return false;
  }

  if (!action_register(&tag_manager_client_move_action)) {
    LOG_ERROR("Failed to register action '%s'", tag_manager_client_move_action.name);
    action_unregister(tag_manager_view_action.name);
    action_unregister(tag_manager_toggle_action.name);
    return false;
  }

  return true;
}

static void
tag_manager_unregister_actions(void)
{
  action_unregister(tag_manager_view_action.name);
  action_unregister(tag_manager_toggle_action.name);
  action_unregister(tag_manager_client_move_action.name);
}

/*
 * Request executors
 */

/*
 * Handle REQ_TAG_VIEW - view specific tag
 * data: pointer to uint32_t tag index (1-9, 0-based internally)
 */
void
tag_manager_handle_view_request(RequestType type, TargetID target, void* data)
{
  (void) type;

  if (target == TARGET_ID_NONE) {
    LOG_DEBUG("Tag view: no target");
    return;
  }

  HubTarget* t = hub_get_target_by_id(target);
  if (t == NULL || t->type_id != hub_get_target_type_id_by_name("monitor")) {
    LOG_DEBUG("Tag view: invalid target");
    return;
  }

  Monitor* m = (Monitor*) t;

  /* Get tag index from data */
  uint32_t tag_index = 0;
  if (data != NULL) {
    tag_index = *(uint32_t*) data;
  }

  /* Convert to valid range (1-9 -> 0-8) */
  if (tag_index < 1 || tag_index > TAG_NUM_TAGS) {
    LOG_DEBUG("Tag view: invalid tag index %u (expected 1-%d)",
              tag_index, TAG_NUM_TAGS);
    return;
  }

  tag_index = tag_index - 1; /* Convert to 0-based */

  LOG_DEBUG("Tag view: switching to tag %u (mask=0x%x)", tag_index, TAG_MASK(tag_index));

  /* Update the visible tag mask (shows only this tag) */
  uint32_t new_mask = TAG_MASK(tag_index);
  tag_manager_set_visible_tags(m, new_mask);
}

/*
 * Handle REQ_TAG_TOGGLE - toggle tag visibility
 * data: pointer to uint32_t tag index (1-9)
 */
void
tag_manager_handle_toggle_request(RequestType type, TargetID target, void* data)
{
  (void) type;

  if (target == TARGET_ID_NONE) {
    LOG_DEBUG("Tag toggle: no target");
    return;
  }

  HubTarget* t = hub_get_target_by_id(target);
  if (t == NULL || t->type_id != hub_get_target_type_id_by_name("monitor")) {
    LOG_DEBUG("Tag toggle: invalid target");
    return;
  }

  Monitor* m = (Monitor*) t;

  /* Get tag index from data */
  uint32_t tag_index = 0;
  if (data != NULL) {
    tag_index = *(uint32_t*) data;
  }

  /* Convert to valid range */
  if (tag_index < 1 || tag_index > TAG_NUM_TAGS) {
    LOG_DEBUG("Tag toggle: invalid tag index %u", tag_index);
    return;
  }

  tag_index = tag_index - 1; /* Convert to 0-based */

  /* Get current visible tags */
  uint32_t current_mask = tag_manager_get_visible_tags(m);
  uint32_t tag_mask     = TAG_MASK(tag_index);

  /* Toggle the tag */
  if ((current_mask & tag_mask) != 0) {
    /* Remove tag */
    current_mask &= ~tag_mask;
  } else {
    /* Add tag */
    current_mask |= tag_mask;
  }

  LOG_DEBUG("Tag toggle: tag=%u, new_mask=0x%x", tag_index, current_mask);

  tag_manager_set_visible_tags(m, current_mask);
}

/*
 * Handle REQ_TAG_CLIENT_TOGGLE - move current client to/from tag
 * data: pointer to uint32_t tag index (1-9)
 */
void
tag_manager_handle_client_tag_toggle(RequestType type, TargetID target, void* data)
{
  (void) type;
  (void) target;

  /* Get the currently focused client */
  Client*        c = NULL;
  extern Client* focus_get_focused_client(void);
  c = focus_get_focused_client();

  if (c == NULL) {
    LOG_DEBUG("Tag client toggle: no focused client");
    return;
  }

  /* Get tag index from data */
  uint32_t tag_index = 0;
  if (data != NULL) {
    tag_index = *(uint32_t*) data;
  }

  /* Convert to valid range */
  if (tag_index < 1 || tag_index > TAG_NUM_TAGS) {
    LOG_DEBUG("Tag client toggle: invalid tag index %u", tag_index);
    return;
  }

  tag_index = tag_index - 1; /* Convert to 0-based */

  /* Toggle the tag on the client */
  if (client_has_tag(c, tag_index)) {
    client_remove_tag(c, tag_index);
  } else {
    client_add_tag(c, tag_index);
  }

  LOG_DEBUG("Tag client toggle: client=%u, new_tags=0x%x", c->window, c->tags);

  /* Update visibility for the client */
  /* Get the client's monitor */
  Monitor* m = client_get_monitor(c);
  if (m != NULL) {
    /* Check if client should be visible on this monitor */
    uint32_t visible_tags      = tag_manager_get_visible_tags(m);
    bool     should_be_visible = (c->tags & visible_tags) != 0;

    if (should_be_visible && !client_is_mapped(c)) {
      client_show(c->window);
      client_set_mapped(c, true);
    } else if (!should_be_visible && client_is_mapped(c)) {
      client_hide(c->window);
      client_set_mapped(c, false);
    }
  }

  /* Emit client tag changed event */
  hub_emit(EVT_TAG_CHANGED, c->target.id, NULL);
  tag_manager_request_tile(client_get_monitor(c));
}

/*
 * Give a client with no tags the tags of the view it appeared on.
 *
 * Client creation leaves tags empty because the visible mask lives in this
 * component's state machine. The manage event is where the two meet, so a
 * window that maps while another tag is viewed joins that tag instead of the
 * one that was visible when the monitor was created.
 *
 * Only a client with no tags is given some. A client that already carries tags
 * was put on them by the user, and a window its client withdrew and mapped
 * again has to come back on those tags rather than on whichever tag happens to
 * be viewed when it returns.
 */
static void
tag_manager_on_client_managed(Event e)
{
  Client* c = client_get_by_window((xcb_window_t) e.target);
  if (c == NULL)
    return;

  Monitor* m = client_get_monitor(c);
  if (m == NULL)
    return;

  if (c->tags != 0)
    return;

  uint32_t visible_tags = tag_manager_get_visible_tags(m);
  client_set_tags(c, visible_tags);

  LOG_DEBUG("Client %u managed on monitor %lu with tags=0x%x",
            c->window, (unsigned long) m->target.id, visible_tags);
}

/*
 * Listener callback - subscribes to relevant events
 */
void
tag_manager_listener(Event e)
{
  if (e.type == EVT_CLIENT_MANAGED)
    tag_manager_on_client_managed(e);
}

/*
 * Adoption hook - initialize tag manager data for a monitor
 */
void
tag_manager_on_adopt(HubTarget* target)
{
  if (target == NULL || target->type_id != hub_get_target_type_id_by_name("monitor")) {
    return;
  }

  Monitor* m = (Monitor*) target;

  /* Create the tag view SM */
  StateMachine* sm = tag_manager_get_view_sm(m);
  if (sm == NULL) {
    LOG_ERROR("Failed to create tag view SM on adoption");
    return;
  }

  /* Initialize with default tag 1 visible */
  uint32_t* tag_mask = (uint32_t*) sm_get_data(sm);
  if (tag_mask != NULL) {
    *tag_mask = TAG_MASK(0); /* Tag 1 (index 0) */
  }

  LOG_DEBUG("Tag manager adopted by monitor: %lu", (unsigned long) target->id);
}

/*
 * Unadoption hook - cleanup tag manager data
 */
void
tag_manager_on_unadopt(HubTarget* target)
{
  if (target == NULL || target->type_id != hub_get_target_type_id_by_name("monitor")) {
    return;
  }

  Monitor* m = (Monitor*) target;

  /* Removing the entry destroys the machine, and the machine frees the tag
   * mask it owns. */
  monitor_set_sm(m, TAG_VIEW_SM_NAME, NULL);

  LOG_DEBUG("Tag manager unadopted by monitor: %lu", (unsigned long) target->id);
}

/*
 * Internal executor for hub routing
 */
static void
tag_manager_executor(struct HubRequest* req)
{
  if (req == NULL)
    return;

  switch (req->type) {
  case REQ_TAG_VIEW:
    tag_manager_handle_view_request(req->type, req->target, req->data);
    break;
  case REQ_TAG_TOGGLE:
    tag_manager_handle_toggle_request(req->type, req->target, req->data);
    break;
  case REQ_TAG_CLIENT_TOGGLE:
    tag_manager_handle_client_tag_toggle(req->type, req->target, req->data);
    break;
  case REQ_CLIENT_VISIBLE:
    tag_manager_answer_visible(req->target, req->data);
    break;
  default:
    LOG_DEBUG("Tag manager: unhandled request type %u", req->type);
    break;
  }
}

/*
 * Component initialization
 */
bool
tag_manager_component_init(void)
{
  /* Check if already registered in hub */
  HubComponent* existing = hub_get_component_by_name(TAG_MANAGER_COMPONENT_NAME);
  if (existing != NULL) {
    if (tag_manager_component_instance.initialized) {
      LOG_DEBUG("Tag manager component already initialized");
      return true;
    }
    LOG_DEBUG("Tag manager component registered with hub, completing init");
    tag_manager_component_instance.initialized = true;
    return true;
  }

  /* Reset if inconsistent state */
  if (tag_manager_component_instance.initialized) {
    LOG_DEBUG("Tag manager component in inconsistent state, resetting");
    tag_manager_component_instance.initialized = false;
  }

  LOG_DEBUG("Initializing tag manager component");

  /* Set the executor */
  tag_manager_component()->executor = tag_manager_executor;

  if (!tag_manager_register_actions())
    return false;

  /* Register with hub */
  if (hub_register_component(tag_manager_component()) != 0) {
    tag_manager_unregister_actions();
    return false;
  }

  /* Subscribe to events we care about */
  hub_subscribe(EVT_CLIENT_MANAGED, tag_manager_listener, NULL);

  /* Cache the template */
  cached_tag_view_template = tag_view_sm_template_create();

  tag_manager_component_instance.initialized = true;
  LOG_DEBUG("Tag manager component initialized successfully");

  return true;
}

/*
 * Component shutdown
 */
void
tag_manager_component_shutdown(void)
{
  if (!tag_manager_component_instance.initialized) {
    return;
  }

  LOG_DEBUG("Shutting down tag manager component");

  /* Unsubscribe from events */
  hub_unsubscribe(EVT_CLIENT_MANAGED, tag_manager_listener);

  /* Unregister from hub */
  hub_unregister_component(TAG_MANAGER_COMPONENT_NAME);

  tag_manager_unregister_actions();

  /* Free cached template */
  if (cached_tag_view_template != NULL) {
    sm_template_destroy(cached_tag_view_template);
    cached_tag_view_template = NULL;
  }

  tag_manager_component_instance.initialized = false;
  LOG_DEBUG("Tag manager component shutdown complete");
}

/*
 * Get the tag manager component instance
 */
TagManagerComponent*
tag_manager_component_get(void)
{
  return &tag_manager_component_instance;
}

/*
 * Check if component is initialized
 */
bool
tag_manager_component_is_initialized(void)
{
  return tag_manager_component_instance.initialized;
}