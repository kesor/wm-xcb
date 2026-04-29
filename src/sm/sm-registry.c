#include "sm-registry.h"

#include <stdlib.h>
#include <string.h>

#include "sm-instance.h"
#include "wm-log.h"

/* Registry entry - uses separate typed function pointer fields to avoid
 * casting between function pointers and data pointers (UB in standard C) */
typedef struct RegistryEntry {
  const char*  name;
  union {
    SMGuardFn  guard_fn;
    SMActionFn action_fn;
  } fn;
  struct RegistryEntry* next;
  bool is_guard;  /* true for guard entries, false for action entries */
} RegistryEntry;

/* Registry buckets */
#define GUARD_REGISTRY_BUCKETS 16
#define ACTION_REGISTRY_BUCKETS 16

static RegistryEntry* guard_registry[GUARD_REGISTRY_BUCKETS];
static RegistryEntry* action_registry[ACTION_REGISTRY_BUCKETS];

static bool initialized = false;

static uint32_t
hash_string(const char* s)
{
  uint32_t hash = 5381;
  while (*s) {
    hash = ((hash << 5) + hash) + (uint32_t) *s;
    s++;
  }
  return hash;
}

static void*
lookup_in_bucket(RegistryEntry** buckets, int num_buckets, const char* name, bool is_guard)
{
  if (name == NULL)
    return NULL;

  uint32_t hash = hash_string(name) % (uint32_t) num_buckets;
  RegistryEntry* entry = buckets[hash];

  while (entry != NULL) {
    if (entry->is_guard == is_guard && strcmp(entry->name, name) == 0) {
      /* Return the appropriate function pointer from the union */
      return is_guard ? (void*) entry->fn.guard_fn : (void*) entry->fn.action_fn;
    }
    entry = entry->next;
  }

  return NULL;
}

static void
register_in_bucket(RegistryEntry** buckets, int num_buckets,
                   const char* name, void* fn, bool is_guard)
{
  if (name == NULL || fn == NULL)
    return;

  uint32_t hash = hash_string(name) % (uint32_t) num_buckets;

  /* Check if already registered */
  if (lookup_in_bucket(buckets, num_buckets, name, is_guard) != NULL) {
    LOG_WARN("Function '%s' already registered", name);
    return;
  }

  RegistryEntry* entry = malloc(sizeof(RegistryEntry));
  if (entry == NULL) {
    LOG_ERROR("Failed to allocate registry entry");
    return;
  }

  entry->name    = name;
  entry->is_guard = is_guard;
  /* Store in union field based on type */
  if (is_guard) {
    entry->fn.guard_fn = (SMGuardFn) fn;
  } else {
    entry->fn.action_fn = (SMActionFn) fn;
  }
  entry->next = buckets[hash];
  buckets[hash] = entry;
}

static void
unregister_from_bucket(RegistryEntry** buckets, int num_buckets,
                       const char* name)
{
  if (name == NULL)
    return;

  uint32_t hash = hash_string(name) % (uint32_t) num_buckets;
  RegistryEntry** prev = &buckets[hash];
  RegistryEntry* curr = *prev;

  while (curr != NULL) {
    if (strcmp(curr->name, name) == 0) {
      *prev = curr->next;
      free(curr);
      return;
    }
    prev = &curr->next;
    curr = curr->next;
  }
}

void
sm_registry_init(void)
{
  if (initialized)
    return;

  memset(guard_registry, 0, sizeof(guard_registry));
  memset(action_registry, 0, sizeof(action_registry));

  initialized = true;
  LOG_DEBUG("SM registry initialized");
}

/*
 * Ensure registry is initialized.
 * Called automatically by registration functions.
 * Returns true if initialization succeeded or already initialized.
 */
static bool
ensure_initialized(void)
{
  if (!initialized) {
    sm_registry_init();
  }
  return initialized;
}

void
sm_registry_shutdown(void)
{
  /* Always free buckets, regardless of initialized state */
  if (!initialized) {
    /* Buckets might have been allocated without init call */
    memset(guard_registry, 0, sizeof(guard_registry));
    memset(action_registry, 0, sizeof(action_registry));
    return;
  }

  /* Free all guard entries */
  for (int i = 0; i < GUARD_REGISTRY_BUCKETS; i++) {
    RegistryEntry* curr = guard_registry[i];
    while (curr != NULL) {
      RegistryEntry* next = curr->next;
      free(curr);
      curr = next;
    }
    guard_registry[i] = NULL;
  }

  /* Free all action entries */
  for (int i = 0; i < ACTION_REGISTRY_BUCKETS; i++) {
    RegistryEntry* curr = action_registry[i];
    while (curr != NULL) {
      RegistryEntry* next = curr->next;
      free(curr);
      curr = next;
    }
    action_registry[i] = NULL;
  }

  initialized = false;
  LOG_DEBUG("SM registry shut down");
}

void
sm_register_guard(const char* name, SMGuardFn fn)
{
  if (!ensure_initialized())
    return;
  register_in_bucket(guard_registry, GUARD_REGISTRY_BUCKETS, name, fn, true);
}

void
sm_unregister_guard(const char* name)
{
  if (!initialized)
    return;
  unregister_from_bucket(guard_registry, GUARD_REGISTRY_BUCKETS, name);
}

void
sm_register_action(const char* name, SMActionFn fn)
{
  if (!ensure_initialized())
    return;
  register_in_bucket(action_registry, ACTION_REGISTRY_BUCKETS, name, fn, false);
}

void
sm_unregister_action(const char* name)
{
  if (!initialized)
    return;
  unregister_from_bucket(action_registry, ACTION_REGISTRY_BUCKETS, name);
}

SMGuardFn
sm_lookup_guard(const char* name)
{
  return (SMGuardFn) lookup_in_bucket(guard_registry, GUARD_REGISTRY_BUCKETS, name, true);
}

SMActionFn
sm_lookup_action(const char* name)
{
  return (SMActionFn) lookup_in_bucket(action_registry, ACTION_REGISTRY_BUCKETS, name, false);
}

bool
sm_run_guard(StateMachine* sm, const char* guard_name, void* data)
{
  if (guard_name == NULL)
    return true;  /* No guard = always allowed */

  SMGuardFn guard = sm_lookup_guard(guard_name);
  if (guard == NULL) {
    LOG_WARN("Guard '%s' not found, allowing transition", guard_name);
    return true;  /* Failsafe: allow if guard not found */
  }

  return guard(sm, data);
}

bool
sm_run_action(StateMachine* sm, const char* action_name, void* data)
{
  if (action_name == NULL)
    return true;  /* No action = success */

  SMActionFn action = sm_lookup_action(action_name);
  if (action == NULL) {
    LOG_WARN("Action '%s' not found", action_name);
    return false;  /* Fail if action not found */
  }

  return action(sm, data);
}