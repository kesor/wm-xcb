#include <stdlib.h>
#include <string.h>

#include "wm-hub.h"
#include "wm-log.h"

/* Registry data structures */
#define MAX_COMPONENTS 64
#define MAX_TARGETS    256
#define MAX_REQUEST_TYPES 256

static HubComponent* components[MAX_COMPONENTS];
static uint32_t      component_count = 0;

/* Component lookup by name */
#define MAX_NAME_LEN 64
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

/* Target lookup by ID */
static HubTarget* target_by_id[MAX_TARGETS];

/* Targets grouped by type (array of arrays) */
static HubTarget* targets_by_type[TARGET_TYPE_COUNT][MAX_TARGETS];
static uint32_t   targets_by_type_count[TARGET_TYPE_COUNT];

/* Forward declarations */
static void component_add_to_request_type_index(HubComponent* comp);

void
hub_init(void)
{
  LOG_DEBUG("Initializing hub registry");

  component_count        = 0;
  name_entry_count       = 0;
  target_count           = 0;

  /* Clear all arrays */
  memset(components, 0, sizeof(components));
  memset(component_by_name, 0, sizeof(component_by_name));
  memset(component_by_request_type, 0, sizeof(component_by_request_type));
  memset(targets, 0, sizeof(targets));
  memset(target_by_id, 0, sizeof(target_by_id));
  memset(targets_by_type, 0, sizeof(targets_by_type));
  memset(targets_by_type_count, 0, sizeof(targets_by_type_count));
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
    target_by_id[targets[target_count]->id] = NULL;
  }

  component_count = 0;
  name_entry_count = 0;
  target_count = 0;
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

  /* Remove from request type index */
  if (comp->requests != NULL) {
    for (int i = 0; comp->requests[i] != 0; i++) {
      RequestType rt = comp->requests[i];
      if (rt < MAX_REQUEST_TYPES) {
        if (component_by_request_type[rt] == comp) {
          component_by_request_type[rt] = NULL;
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

  /* Remove from main component array */
  for (uint32_t i = 0; i < component_count; i++) {
    if (components[i] == comp) {
      components[i] = components[component_count - 1];
      component_count--;
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

  /* Add to main target array */
  targets[target_count++] = target;
  target->registered = true;

  /* Add to ID index */
  if (target->id < MAX_TARGETS) {
    target_by_id[target->id] = target;
  }

  /* Add to type index */
  if (targets_by_type_count[target->type] < MAX_TARGETS) {
    targets_by_type[target->type][targets_by_type_count[target->type]++] = target;
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
      break;
    }
  }

  /* Remove from ID index */
  if (id < MAX_TARGETS) {
    target_by_id[id] = NULL;
  }

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

  if (id >= MAX_TARGETS)
    return NULL;

  return target_by_id[id];
}

HubTarget**
hub_get_targets_by_type(TargetType type)
{
  if (type >= TARGET_TYPE_COUNT)
    return NULL;

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
