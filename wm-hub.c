#include "wm-hub.h"

#include <stdlib.h>
#include <string.h>

/* Registry data structures */
#define MAX_COMPONENTS 64
#define MAX_TARGETS    256
#define MAX_REQUEST_TYPES 256

static HubComponent* components[MAX_COMPONENTS];
static uint32_t      component_count = 0;

/* Component lookup by name */
typedef struct {
  const char*    name;
  HubComponent* comp;
} component_name_entry_t;

static component_name_entry_t component_by_name[MAX_COMPONENTS];
static uint32_t               name_entry_count = 0;

/* Component lookup by request type (array indexed by RequestType) */
static HubComponent* component_by_request_type[MAX_REQUEST_TYPES];

/* Target registry */
static HubTarget* targets[MAX_TARGETS];
static uint32_t   target_count = 0;

/* Target lookup by ID - use hash map for TargetID (uint64_t) beyond MAX_TARGETS */
#define TARGET_ID_MAP_SIZE 512

typedef struct target_id_entry {
  TargetID        id;
  HubTarget*      target;
  struct target_id_entry* next;
} target_id_entry_t;

static target_id_entry_t* target_by_id_map[TARGET_ID_MAP_SIZE];

static uint32_t
target_id_hash(TargetID id)
{
  /* Simple hash function for TargetID */
  return (uint32_t)((id ^ (id >> 16)) % TARGET_ID_MAP_SIZE);
}

/* Targets grouped by type (array of arrays) */
static HubTarget* targets_by_type[TARGET_TYPE_COUNT][MAX_TARGETS];
static uint32_t   targets_by_type_count[TARGET_TYPE_COUNT];

/* Event Bus - maximum number of event types */
#define MAX_EVENT_TYPES 64

/* Event Bus - maximum subscribers per event type */
#define MAX_SUBSCRIBERS 16

/* Event Bus - subscription array per event type */
static struct {
  struct Subscriber subscribers[MAX_SUBSCRIBERS];
  int               count;
} subscribers[MAX_EVENT_TYPES];

/* Forward declarations */
static void component_add_to_request_type_index(HubComponent* comp);
static void target_by_id_map_insert(HubTarget* target);
static void target_by_id_map_remove(TargetID id);
static HubTarget* target_by_id_map_lookup(TargetID id);

/*
 * Clear all registry state.
 * Called on both init and shutdown for consistent cleanup.
 */
static void
clear_registry_state(void)
{
  /* Clear component arrays */
  memset(components, 0, sizeof(components));
  memset(component_by_name, 0, sizeof(component_by_name));
  memset(component_by_request_type, 0, sizeof(component_by_request_type));

  /* Clear target arrays */
  memset(targets, 0, sizeof(targets));
  memset(target_by_id_map, 0, sizeof(target_by_id_map));
  memset(targets_by_type, 0, sizeof(targets_by_type));
  memset(targets_by_type_count, 0, sizeof(targets_by_type_count));

  /* Clear event bus subscriber arrays */
  memset(subscribers, 0, sizeof(subscribers));

  component_count = 0;
  name_entry_count = 0;
  target_count = 0;
}

void
hub_init(void)
{
  LOG_DEBUG("Initializing hub registry");
  clear_registry_state();
}

void
hub_shutdown(void)
{
  LOG_DEBUG("Shutting down hub registry");

  /* Unregister all components */
  while (component_count > 0) {
    component_count--;
    components[component_count]->registered = false;
  }

  /* Unregister all targets */
  while (target_count > 0) {
    target_count--;
    targets[target_count]->registered = false;
  }

  /* Clear all registry state including indexes */
  clear_registry_state();
}

void
hub_register_component(HubComponent* comp)
{
  if (comp == NULL) {
    LOG_ERROR("Cannot register NULL component");
    return;
  }

  if (comp->registered) {
    LOG_ERROR("Component '%s' is already registered", comp->name);
    return;
  }

  if (component_count >= MAX_COMPONENTS) {
    LOG_ERROR("Maximum number of components reached (%d)", MAX_COMPONENTS);
    return;
  }

  /* Add to main component array */
  components[component_count++] = comp;
  comp->registered = true;

  /* Add to name index */
  if (name_entry_count < MAX_COMPONENTS) {
    component_by_name[name_entry_count].name = comp->name;
    component_by_name[name_entry_count].comp = comp;
    name_entry_count++;
  }

  /* Add to request type index */
  component_add_to_request_type_index(comp);

  LOG_DEBUG("Registered component: %s", comp->name);
}

void
hub_unregister_component(const char* name)
{
  if (name == NULL) {
    LOG_ERROR("Cannot unregister component with NULL name");
    return;
  }

  /* Find component by name */
  HubComponent* comp = hub_get_component_by_name(name);
  if (comp == NULL) {
    LOG_ERROR("Component '%s' not found", name);
    return;
  }

  if (!comp->registered) {
    LOG_ERROR("Component '%s' is not registered", name);
    return;
  }

  /* Remove from main component array FIRST */
  for (uint32_t i = 0; i < component_count; i++) {
    if (components[i] == comp) {
      components[i] = components[component_count - 1];
      component_count--;
      break;
    }
  }

  /* Recompute request type mappings - scan remaining components */
  if (comp->requests != NULL) {
    for (int i = 0; comp->requests[i] != 0; i++) {
      RequestType rt = comp->requests[i];
      if (rt < MAX_REQUEST_TYPES) {
        /* Only recompute if this component was the handler */
        if (component_by_request_type[rt] == comp) {
          component_by_request_type[rt] = NULL;
          /* Recompute from remaining registered components */
          for (int32_t j = (int32_t)component_count - 1; j >= 0; j--) {
            HubComponent* other = components[j];
            if (other != NULL && other->requests != NULL) {
              for (int k = 0; other->requests[k] != 0; k++) {
                if (other->requests[k] == rt) {
                  component_by_request_type[rt] = other;
                  j = -1;  /* Signal found, break outer loop */
                  break;
                }
              }
            }
          }
        }
      }
    }
  }

  /* Remove from name index */
  for (uint32_t i = 0; i < name_entry_count; i++) {
    if (component_by_name[i].comp == comp) {
      /* Swap with last and decrement */
      component_by_name[i] = component_by_name[name_entry_count - 1];
      name_entry_count--;
      break;
    }
  }

  comp->registered = false;
  LOG_DEBUG("Unregistered component: %s", name);
}

HubComponent*
hub_get_component_by_name(const char* name)
{
  if (name == NULL)
    return NULL;

  for (uint32_t i = 0; i < name_entry_count; i++) {
    if (component_by_name[i].name != NULL &&
        strcmp(component_by_name[i].name, name) == 0) {
      return component_by_name[i].comp;
    }
  }
  return NULL;
}

HubComponent*
hub_get_component_by_request_type(RequestType type)
{
  if (type >= MAX_REQUEST_TYPES)
    return NULL;

  return component_by_request_type[type];
}

void
hub_register_target(HubTarget* target)
{
  if (target == NULL) {
    LOG_ERROR("Cannot register NULL target");
    return;
  }

  if (target->registered) {
    LOG_ERROR("Target %lu is already registered", (unsigned long)target->id);
    return;
  }

  if (target_count >= MAX_TARGETS) {
    LOG_ERROR("Maximum number of targets reached (%d)", MAX_TARGETS);
    return;
  }

  if (target->id == TARGET_ID_NONE) {
    LOG_ERROR("Cannot register target with ID NONE");
    return;
  }

  if (target->type >= TARGET_TYPE_COUNT) {
    LOG_ERROR("Invalid target type %u", target->type);
    return;
  }

  /* Check for duplicate ID */
  if (hub_get_target_by_id(target->id) != NULL) {
    LOG_ERROR("Target with ID %lu already registered", (unsigned long)target->id);
    return;
  }

  /* Add to main target array */
  targets[target_count++] = target;
  target->registered = true;

  /* Add to ID index (hash map for arbitrary TargetID values) */
  target_by_id_map_insert(target);

  /* Add to type index with NULL terminator */
  if (targets_by_type_count[target->type] < MAX_TARGETS) {
    targets_by_type[target->type][targets_by_type_count[target->type]++] = target;
    /* Ensure NULL terminator */
    if (targets_by_type_count[target->type] < MAX_TARGETS) {
      targets_by_type[target->type][targets_by_type_count[target->type]] = NULL;
    }
  }

  LOG_DEBUG("Registered target: id=%lu, type=%u", (unsigned long)target->id, target->type);
}

void
hub_unregister_target(TargetID id)
{
  if (id == TARGET_ID_NONE) {
    LOG_ERROR("Cannot unregister target with ID NONE");
    return;
  }

  HubTarget* target = hub_get_target_by_id(id);
  if (target == NULL) {
    LOG_ERROR("Target %lu not found", (unsigned long)id);
    return;
  }

  /* Remove from type index */
  for (uint32_t i = 0; i < targets_by_type_count[target->type]; i++) {
    if (targets_by_type[target->type][i] == target) {
      targets_by_type[target->type][i] =
        targets_by_type[target->type][targets_by_type_count[target->type] - 1];
      targets_by_type_count[target->type]--;
      /* Clear vacated slot and ensure NULL terminator */
      targets_by_type[target->type][targets_by_type_count[target->type]] = NULL;
      break;
    }
  }

  /* Remove from ID index */
  target_by_id_map_remove(id);

  /* Remove from main target array */
  for (uint32_t i = 0; i < target_count; i++) {
    if (targets[i] == target) {
      targets[i] = targets[target_count - 1];
      target_count--;
      break;
    }
  }

  target->registered = false;
  LOG_DEBUG("Unregistered target: id=%lu", (unsigned long)id);
}

HubTarget*
hub_get_target_by_id(TargetID id)
{
  if (id == TARGET_ID_NONE)
    return NULL;

  return target_by_id_map_lookup(id);
}

HubTarget**
hub_get_targets_by_type(TargetType type)
{
  if (type >= TARGET_TYPE_COUNT)
    return NULL;

  /* Ensure NULL terminator is set */
  if (targets_by_type_count[type] < MAX_TARGETS) {
    targets_by_type[type][targets_by_type_count[type]] = NULL;
  }

  /* Return pointer to the array - caller should not modify */
  return targets_by_type[type];
}

uint32_t
hub_component_count(void)
{
  return component_count;
}

uint32_t
hub_target_count(void)
{
  return target_count;
}

/* Internal helper: add component to request type index */
static void
component_add_to_request_type_index(HubComponent* comp)
{
  if (comp->requests == NULL)
    return;

  for (int i = 0; comp->requests[i] != 0; i++) {
    RequestType rt = comp->requests[i];
    if (rt < MAX_REQUEST_TYPES) {
      /* If there's already a component for this request type,
       * the last registered one wins (overwrites) */
      component_by_request_type[rt] = comp;
    }
  }
}

/* Hash map implementation for TargetID lookup */
static void
target_by_id_map_insert(HubTarget* target)
{
  uint32_t hash = target_id_hash(target->id);
  target_id_entry_t* entry = malloc(sizeof(target_id_entry_t));
  if (entry == NULL) {
    LOG_ERROR("Failed to allocate target ID map entry");
    return;
  }
  entry->id = target->id;
  entry->target = target;
  entry->next = target_by_id_map[hash];
  target_by_id_map[hash] = entry;
}

static void
target_by_id_map_remove(TargetID id)
{
  uint32_t hash = target_id_hash(id);
  target_id_entry_t** prev = &target_by_id_map[hash];
  target_id_entry_t* curr = *prev;

  while (curr != NULL) {
    if (curr->id == id) {
      *prev = curr->next;
      free(curr);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }
}

static HubTarget*
target_by_id_map_lookup(TargetID id)
{
  uint32_t hash = target_id_hash(id);
  target_id_entry_t* curr = target_by_id_map[hash];

  while (curr != NULL) {
    if (curr->id == id) {
      return curr->target;
    }
    curr = curr->next;
  }
  return NULL;
}

/*
 * Event Bus Implementation
 *
 * Provides pub/sub communication between components.
 * Components emit events when their state machines transition.
 * Other components subscribe to these events.
 */

void
hub_emit(EventType type, TargetID target, void* data)
{
  if (type >= MAX_EVENT_TYPES) {
    return;
  }

  int count = subscribers[type].count;
  if (count == 0) {
    return;
  }

  /* Snapshot subscribers to safely handle unsubscribe during emit */
  struct Subscriber snapshot[MAX_SUBSCRIBERS];
  memcpy(snapshot, subscribers[type].subscribers, (size_t) count * sizeof(struct Subscriber));

  for (int i = 0; i < count; i++) {
    struct Event e = {
      .type     = type,
      .target   = target,
      .data     = data,
      .userdata = snapshot[i].userdata,
    };
    snapshot[i].handler(e);
  }
}

void
hub_subscribe(EventType type, EventHandler handler, void* userdata)
{
  if (type >= MAX_EVENT_TYPES) {
    return;
  }

  if (handler == NULL) {
    return;
  }

  int count = subscribers[type].count;
  if (count >= MAX_SUBSCRIBERS) {
    return;
  }

  /* Check for duplicate handler */
  for (int i = 0; i < count; i++) {
    if (subscribers[type].subscribers[i].handler == handler) {
      return;
    }
  }

  subscribers[type].subscribers[count].handler  = handler;
  subscribers[type].subscribers[count].userdata = userdata;
  subscribers[type].count++;
}

void
hub_unsubscribe(EventType type, EventHandler handler)
{
  if (type >= MAX_EVENT_TYPES) {
    return;
  }

  if (handler == NULL) {
    return;
  }

  int count = subscribers[type].count;
  for (int i = 0; i < count; i++) {
    if (subscribers[type].subscribers[i].handler == handler) {
      /* Remove by shifting remaining subscribers */
      for (int j = i; j < count - 1; j++) {
        subscribers[type].subscribers[j] = subscribers[type].subscribers[j + 1];
      }
      subscribers[type].count--;
      return;
    }
  }
}