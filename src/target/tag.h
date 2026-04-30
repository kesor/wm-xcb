/*
 * Tag Target - Virtual workspace entity for the window manager
 *
 * Tags are virtual workspaces that exist independently of monitors.
 * A tag like "work" or "tag 9" exists whether or not any monitor is viewing it.
 * Monitors "view" tags via the pertag component.
 *
 * Design principles:
 * 1. Tags exist independently - they don't need a monitor to exist
 * 2. Monitors "view" tags - via the pertag component, a monitor indicates which tags it displays
 * 3. Client ↔ Tag relationship - clients have a tagmask; they're visible when their
 *    tags overlap with a monitor's visible tagset
 */

#ifndef _TAG_H_
#define _TAG_H_

#include <stdbool.h>
#include <stdint.h>

#include "wm-hub.h"

/*
 * Number of available tags (standard dwm uses 9 tags, 0-8)
 * This matches MONITOR_NUM_TAGS in monitor.h for consistency
 */
#define TAG_NUM_TAGS   9

/*
 * Tag mask helpers
 * These are compatible with MONITOR_TAG_MASK for easy bitwise operations
 */
#define TAG_MASK(n)    ((uint32_t) 1 << ((n) % TAG_NUM_TAGS))
#define TAG_ALL_TAGS   ((uint32_t) ((1 << TAG_NUM_TAGS) - 1))
#define TAG_VALID(tag) ((tag) >= 0 && (tag) < TAG_NUM_TAGS)

/*
 * Tag structure
 * Represents a virtual workspace (tag 0-8).
 *
 * Note: Tags are lightweight entities. They primarily exist to be referenced
 * by the pertag component which bridges Monitor ↔ Tag relationships.
 */
typedef struct Tag {
  /* Base target for Hub registration */
  HubTarget target;

  /* Tag identity */
  int      index; /* 0-8 (matching standard tag indices) */
  char*    name;  /* optional: "work", "web", "9", NULL for default */
  uint32_t mask;  /* bitmask for this tag (1 << index) */

  /* Next tag in the global tag list */
  struct Tag* next;

} Tag;

/*
 * Tag lifecycle
 */

/*
 * Create a new Tag with the given index.
 * @param index  Tag index (0-8)
 * @param name   Optional name (can be NULL)
 * @return       New Tag or NULL on failure
 */
Tag* tag_create(int index, const char* name);

/*
 * Destroy a Tag.
 * @param t  Tag to destroy (can be NULL)
 */
void tag_destroy(Tag* t);

/*
 * Tag list management
 */

/*
 * Initialize the global tag list with default tags (0-8).
 * Called at startup.
 */
void tag_list_init(void);

/*
 * Shutdown the global tag list.
 * Destroys all tags and cleans up resources.
 */
void tag_list_shutdown(void);

/*
 * Get the first tag in the global list.
 * @return  First tag or NULL if no tags
 */
Tag* tag_list_get_first(void);

/*
 * Get the next tag after the given one.
 * @param t  Current tag
 * @return   Next tag or NULL
 */
Tag* tag_list_get_next(Tag* t);

/*
 * Iterate over all tags.
 * @param callback  Function to call for each tag
 */
void tag_foreach(void (*callback)(Tag* t));

/*
 * Get a tag by index.
 * @param index  Tag index (0-8)
 * @return       Tag at that index or NULL
 */
Tag* tag_get_by_index(int index);

/*
 * Get a tag by name.
 * @param name  Tag name to search for
 * @return      First tag with matching name, or NULL
 */
Tag* tag_get_by_name(const char* name);

/*
 * Get the TargetID for a tag by index.
 * @param index  Tag index (0-8)
 * @return       TargetID for that tag, or TARGET_ID_NONE
 */
TargetID tag_get_id_by_index(int index);

/*
 * Tag lookup helpers
 */

/*
 * Check if a TargetID refers to a TAG target.
 * @param id  TargetID to check
 * @return    true if this is a TAG target
 */
bool tag_is_tag_target(TargetID id);

/*
 * Get a Tag by its TargetID.
 * @param id  TargetID (must be a TAG target)
 * @return    Tag or NULL
 */
Tag* tag_get_by_id(TargetID id);

/*
 * Convert a tag index to a TargetID for hub routing.
 * Uses a reserved range for TAG targets (above MONITOR range).
 * @param index  Tag index (0-8)
 * @return       TargetID for routing
 */
TargetID tag_index_to_target_id(int index);

/*
 * Convert a TargetID back to a tag index.
 * @param id  TargetID (must be from tag_index_to_target_id)
 * @return    Tag index (0-8) or -1 if invalid
 */
int tag_target_id_to_index(TargetID id);

/*
 * Utility functions
 */

/*
 * Get the tag name (returns default name if none set).
 * @param t  Tag
 * @return   Tag name (never NULL)
 */
const char* tag_get_name(const Tag* t);

/*
 * Set the tag name.
 * @param t    Tag
 * @param name New name (ownership transferred to tag)
 */
void tag_set_name(Tag* t, char* name);

/*
 * Check if a tag index is valid.
 * @param index  Index to check
 * @return       true if valid (0-8)
 */
bool tag_index_valid(int index);

/*
 * Iterate over tags matching a bitmask.
 * @param mask     Bitmask of tags to iterate
 * @param callback Function to call for each matching tag
 */
void tag_iterate_by_mask(uint32_t mask, void (*callback)(Tag* t));

#endif /* _TAG_H_ */
