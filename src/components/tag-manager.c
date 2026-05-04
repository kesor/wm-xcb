/*
 * Tag Manager Component Implementation
 *
 * Handles tag view state for monitors.
 * - Executor handles REQ_TAG_VIEW, REQ_TAG_TOGGLE, REQ_TAG_CLIENT_TOGGLE
 * - Uses TagViewSM to track visible tag bitmask
 * - On tag change: shows/hides clients based on tag membership
 * - Emits EVT_TAG_CHANGED event
 */

#include <stdlib.h>
#include <string.h>

#include "../sm/sm-instance.h"
#include "../sm/sm-registry.h"
#include "../sm/sm-template.h"
#include "../target/client.h"
#include "../target/monitor.h"
#include "../target/tag.h"
#include "tag-manager.h"
#include "wm-hub.h"
#include "wm-log.h"

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

  if (sm == NULL || sm->owner == NULL)
    return;

  Monitor* m = (Monitor*) sm->owner;

  /* Get the tag mask from the SM data (stored as void*) */
  uint32_t* tag_mask_ptr = (uint32_t*) sm->data;
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

  /* Store tag mask in SM data */
  sm->data = tag_mask;

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
  if (sm == NULL || sm->data == NULL)
    return 0;

  uint32_t* tag_mask = (uint32_t*) sm->data;
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

  uint32_t* current_mask = (uint32_t*) sm->data;
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
    if (c->monitor == m && c->managed) {
      bool should_be_visible = (c->tags & visible_tags) != 0;

      if (should_be_visible && !c->mapped) {
        /* Show client */
        LOG_DEBUG("Showing client %u (tags=0x%x, visible=0x%x)",
                  c->window, c->tags, visible_tags);
        client_show(c->window);
        c->mapped = true;
      } else if (!should_be_visible && c->mapped) {
        /* Hide client */
        LOG_DEBUG("Hiding client %u (tags=0x%x, visible=0x%x)",
                  c->window, c->tags, visible_tags);
        client_hide(c->window);
        c->mapped = false;
      }
    }
    c = client_get_next(c);
  }
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

  tag_index         = tag_index - 1; /* Convert to 0-based */
  uint32_t tag_mask = TAG_MASK(tag_index);

  /* Toggle the tag on the client */
  if ((c->tags & tag_mask) != 0) {
    /* Remove tag */
    c->tags &= ~tag_mask;
  } else {
    /* Add tag */
    c->tags |= tag_mask;
  }

  LOG_DEBUG("Tag client toggle: client=%u, new_tags=0x%x", c->window, c->tags);

  /* Update visibility for the client */
  /* Get the client's monitor */
  Monitor* m = client_get_monitor(c);
  if (m != NULL) {
    /* Check if client should be visible on this monitor */
    uint32_t visible_tags      = tag_manager_get_visible_tags(m);
    bool     should_be_visible = (c->tags & visible_tags) != 0;

    if (should_be_visible && !c->mapped) {
      client_show(c->window);
      c->mapped = true;
    } else if (!should_be_visible && c->mapped) {
      client_hide(c->window);
      c->mapped = false;
    }
  }

  /* Emit client tag changed event */
  hub_emit(EVT_TAG_CHANGED, c->target.id, NULL);
}

/*
 * Listener callback - subscribes to relevant events
 */
void
tag_manager_listener(Event e)
{
  (void) e;
  /* Currently no external events to handle in the listener.
   * The executor handles all requests directly.
   * This is here for future extension if needed. */
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
  uint32_t* tag_mask = (uint32_t*) sm->data;
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

  /* Get the SM and free its data */
  StateMachine* sm = monitor_get_sm(m, TAG_VIEW_SM_NAME);
  if (sm != NULL && sm->data != NULL) {
    free(sm->data);
    sm->data = NULL;
  }

  /* Clear from monitor */
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

  /* Register with hub */
  hub_register_component(tag_manager_component());

  /* Subscribe to events we care about */
  hub_subscribe(EVT_TAG_CHANGED, tag_manager_listener, NULL);

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
  hub_unsubscribe(EVT_TAG_CHANGED, tag_manager_listener);

  /* Unregister from hub */
  hub_unregister_component(TAG_MANAGER_COMPONENT_NAME);

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