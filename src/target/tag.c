/*
 * Tag Target Implementation
 *
 * Tags are virtual workspaces that exist independently of monitors.
 * This implementation provides:
 * - Tag list management (9 default tags at startup)
 * - Hub integration (tags are registered as TARGET_TYPE_TAG)
 * - Tag lookup by index, name, and TargetID
 *
 * Architecture:
 * - Tags are created at startup via tag_list_init()
 * - Each tag is registered with the Hub
 * - The pertag component bridges MONITOR → TAG targets
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tag.h"
#include "wm-hub.h"
#include "wm-log.h"

/*
 * Global tag list
 * Initialized with 9 default tags at startup
 */
static Tag* tag_list = NULL;

/*
 * Get default tag name for an index.
 * Returns a static buffer - not for long-term storage.
 * Copy the result if needed for later use.
 */
static const char*
tag_default_name(int index)
{
  static char name[16];
  (void) snprintf(name, sizeof(name), "%d", index + 1); /* 1-indexed for user display */
  return name;
}

/*
 * Create a new Tag.
 */
Tag*
tag_create(int index, const char* name)
{
  /* Validate index */
  if (!TAG_VALID(index)) {
    LOG_ERROR("Cannot create tag with invalid index: %d", index);
    return NULL;
  }

  /* Check for duplicate - if a tag with this index already exists */
  if (tag_get_by_index(index) != NULL) {
    LOG_ERROR("Tag with index %d already exists", index);
    return NULL;
  }

  LOG_DEBUG("Creating tag: index=%d, name=%s", index, name ? name : "(default)");

  Tag* t = malloc(sizeof(Tag));
  if (t == NULL) {
    LOG_ERROR("Failed to allocate Tag");
    return NULL;
  }

  /* Initialize base target
   * TargetID uses a reserved range above actual window/output IDs
   * Tag targets use IDs: TAG_ID_BASE + index
   */
  t->target.id         = tag_index_to_target_id(index);
  t->target.type       = TARGET_TYPE_TAG;
  t->target.registered = false;

  /* Initialize tag properties */
  t->index = index;
  t->mask  = TAG_MASK(index);
  t->name  = name ? strdup(name) : NULL;

  /* Add to list */
  t->next  = tag_list;
  tag_list = t;

  /* Register with Hub */
  hub_register_target(&t->target);

  /* Verify registration succeeded */
  if (!t->target.registered) {
    LOG_ERROR("Failed to register tag target: index=%d", index);
    /* Remove from list */
    Tag** prev = &tag_list;
    while (*prev != NULL && *prev != t) {
      prev = &(*prev)->next;
    }
    if (*prev == t) {
      *prev = t->next;
    }
    if (t->name != NULL) {
      free(t->name);
    }
    free(t);
    return NULL;
  }

  LOG_DEBUG("Tag created: index=%d, id=%lu", index, (unsigned long) t->target.id);
  return t;
}

/*
 * Destroy a Tag.
 */
void
tag_destroy(Tag* t)
{
  if (t == NULL)
    return;

  LOG_DEBUG("Destroying tag: index=%d", t->index);

  /* Remove from list */
  if (tag_list == t) {
    tag_list = t->next;
  } else {
    Tag* prev = tag_list;
    while (prev != NULL && prev->next != t) {
      prev = prev->next;
    }
    if (prev != NULL) {
      prev->next = t->next;
    }
  }

  /* Unregister from Hub */
  if (t->target.registered) {
    hub_unregister_target(t->target.id);
  }

  /* Free name */
  if (t->name != NULL) {
    free(t->name);
  }

  /* Free tag */
  free(t);

  LOG_DEBUG("Tag destroyed");
}

/*
 * Initialize the global tag list with default tags (0-8).
 * Clears any stale tags from previous runs before creating new ones.
 */
void
tag_list_init(void)
{
  /* Clear any stale tags from previous runs */
  while (tag_list != NULL) {
    Tag* next = tag_list->next;
    /* Only free if not registered with hub (orphaned) */
    if (tag_list->target.registered) {
      hub_unregister_target(tag_list->target.id);
    }
    if (tag_list->name != NULL) {
      free(tag_list->name);
    }
    free(tag_list);
    tag_list = next;
  }
  tag_list = NULL;

  /* Create 9 default tags (indices 0-8) */
  LOG_DEBUG("Initializing default tags (0-8)");
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    Tag* t = tag_create(i, NULL);
    if (t == NULL) {
      LOG_ERROR("Failed to create default tag %d", i);
      /* Continue creating remaining tags */
    }
  }

  LOG_DEBUG("Tag list initialized with %d tags", TAG_NUM_TAGS);
}

/*
 * Shutdown the global tag list.
 */
void
tag_list_shutdown(void)
{
  while (tag_list != NULL) {
    Tag* next = tag_list->next;
    tag_destroy(tag_list);
    tag_list = next;
  }
  tag_list = NULL;

  LOG_DEBUG("Tag list shutdown complete");
}

/*
 * Get the first tag in the global list.
 */
Tag*
tag_list_get_first(void)
{
  return tag_list;
}

/*
 * Get the next tag after the given one.
 */
Tag*
tag_list_get_next(Tag* t)
{
  if (t == NULL)
    return NULL;
  return t->next;
}

/*
 * Iterate over all tags.
 */
void
tag_foreach(void (*callback)(Tag* t))
{
  Tag* current = tag_list;
  while (current != NULL) {
    Tag* next = current->next;
    callback(current);
    current = next;
  }
}

/*
 * Get a tag by index.
 */
Tag*
tag_get_by_index(int index)
{
  if (!TAG_VALID(index))
    return NULL;

  for (Tag* t = tag_list; t != NULL; t = t->next) {
    if (t->index == index)
      return t;
  }
  return NULL;
}

/*
 * Get a tag by name.
 */
Tag*
tag_get_by_name(const char* name)
{
  if (name == NULL)
    return NULL;

  for (Tag* t = tag_list; t != NULL; t = t->next) {
    if (t->name != NULL && strcmp(t->name, name) == 0)
      return t;
  }
  return NULL;
}

/*
 * Get the TargetID for a tag by index.
 */
TargetID
tag_get_id_by_index(int index)
{
  Tag* t = tag_get_by_index(index);
  if (t == NULL)
    return TARGET_ID_NONE;
  return t->target.id;
}

/*
 * Check if a TargetID refers to a TAG target.
 * TAG targets use IDs in the reserved range above MONITOR range.
 */
bool
tag_is_tag_target(TargetID id)
{
  /* TAG targets use IDs: TAG_ID_BASE + index (0-8)
   * TAG_ID_BASE is set to avoid collisions with window IDs and output IDs
   */
  return tag_target_id_to_index(id) >= 0;
}

/*
 * Get a Tag by its TargetID.
 */
Tag*
tag_get_by_id(TargetID id)
{
  HubTarget* t = hub_get_target_by_id(id);
  if (t == NULL || t->type != TARGET_TYPE_TAG)
    return NULL;
  return (Tag*) t;
}

/*
 * Convert a tag index to a TargetID for hub routing.
 * Uses a reserved range: 0xF000000000000000 + index
 * This avoids collisions with:
 * - Window IDs (typically small positive integers)
 * - RandR output IDs (XIDs, typically larger but within XID range)
 */
TargetID
tag_index_to_target_id(int index)
{
  if (!TAG_VALID(index))
    return TARGET_ID_NONE;
  /* Use high bits to avoid collision with normal X11 IDs */
  return (TargetID) (0xF000000000000000ULL | (uint64_t) index);
}

/*
 * Convert a TargetID back to a tag index.
 */
int
tag_target_id_to_index(TargetID id)
{
  /* Check if this is a TAG target by checking the base */
  const TargetID TAG_ID_BASE = 0xF000000000000000ULL;
  if ((id & 0xF000000000000000ULL) == TAG_ID_BASE) {
    int index = (int) (id & 0xFFFFFFFFULL);
    if (TAG_VALID(index))
      return index;
  }
  return -1;
}

/*
 * Get the tag name (returns default name if none set).
 * Note: Returns a pointer to an internal static buffer for default names.
 * Copy the result if needed for later use.
 */
const char*
tag_get_name(const Tag* t)
{
  if (t == NULL)
    return "(null)";
  if (t->name != NULL)
    return t->name;
  return tag_default_name(t->index);
}

/*
 * Set the tag name.
 */
void
tag_set_name(Tag* t, char* name)
{
  if (t == NULL)
    return;
  if (t->name != NULL) {
    free(t->name);
  }
  t->name = name;
}

/*
 * Check if a tag index is valid.
 */
bool
tag_index_valid(int index)
{
  return TAG_VALID(index);
}

/*
 * Iterate over tags matching a bitmask.
 */
void
tag_iterate_by_mask(uint32_t mask, void (*callback)(Tag* t))
{
  if (callback == NULL)
    return;

  for (Tag* t = tag_list; t != NULL; t = t->next) {
    if ((mask & t->mask) != 0) {
      callback(t);
    }
  }
}
