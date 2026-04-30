/*
 * Pertag Component - Per-Tag State Management for Monitors
 *
 * This component bridges MONITOR → TAG targets by storing per-tag state.
 * When a monitor switches between tags, the pertag component:
 * 1. Saves the current monitor state (mfact, nmaster, layout, etc.) to the old tag
 * 2. Restores the state for the new tag (or uses defaults if first visit)
 *
 * Design principles:
 * - Pertag allocates and owns its own data
 * - Data is stored in the monitor's state machine storage by name "pertag"
 * - Components query pertag data via pertag_get_data()
 *
 * Architecture:
 * - pertag component is adopted by each Monitor
 * - Stores arrays indexed by tag number (0-8)
 * - Handles tag switching requests via hub requests
 */

#ifndef _PERTAG_H_
#define _PERTAG_H_

#include <stdbool.h>
#include <stdint.h>

#include "../target/tag.h"
#include "wm-hub.h"

/*
 * Number of tags (matches TAG_NUM_TAGS)
 */
#define PERTAG_NUM_TAGS TAG_NUM_TAGS

/*
 * Pertag structure - per-monitor per-tag state
 *
 * Attached to each Monitor as component data.
 * Arrays are indexed by tag number:
 * - Index 0: "all tags" view (aggregated/default state)
 * - Index 1-9: specific tag state (matching tag indices 0-8)
 */
typedef struct Pertag {
  /* Current tag index (0 = all tags, 1-9 = specific tags) */
  int curtag;
  int prevtag; /* Previous tag for toggle functionality */

  /* Per-tag layout pointers (index 0 = all, 1-9 = specific) */
  void* layouts[PERTAG_NUM_TAGS + 1][2]; /* Two layout slots per tag */

  /* Per-tag master factor */
  float mfacts[PERTAG_NUM_TAGS + 1];

  /* Per-tag number of masters */
  int nmasters[PERTAG_NUM_TAGS + 1];

  /* Per-tag layout slot selection */
  int sellts[PERTAG_NUM_TAGS + 1];

  /* Per-tag bar visibility */
  bool showbars[PERTAG_NUM_TAGS + 1];

  /* Per-tag focused window (by TargetID) */
  TargetID focused[PERTAG_NUM_TAGS + 1];

  /* Pointer back to owning monitor */
  struct Monitor* monitor;

} Pertag;

/*
 * Pertag component lifecycle
 */

/*
 * Initialize the pertag component.
 * Allocates Pertag data for a monitor and stores it via monitor_set_sm().
 * @param target  Target that adopted this component (must be MONITOR)
 */
void pertag_on_adopt(HubTarget* target);

/*
 * Cleanup pertag component data.
 * Frees the Pertag data allocated in pertag_on_adopt().
 * @param target  Target that is unadopting this component
 */
void pertag_on_unadopt(HubTarget* target);

/*
 * Pertag query functions
 */

/*
 * Get the current tag index for a monitor.
 * @param monitor  Monitor pointer
 * @return          Current tag index (1-9 for specific tags, 0 for all tags view)
 */
int pertag_get_curtag(const struct Monitor* monitor);

/*
 * Get the previous tag index for a monitor.
 * @param monitor  Monitor pointer
 * @return          Previous tag index (1-9), or 0 for all
 */
int pertag_get_prevtag(const struct Monitor* monitor);

/*
 * Get the master factor for a specific tag on a monitor.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @return          Master factor for that tag
 */
float pertag_get_mfact(const struct Monitor* monitor, int tag);

/*
 * Get the number of masters for a specific tag on a monitor.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @return          Number of masters for that tag
 */
int pertag_get_nmaster(const struct Monitor* monitor, int tag);

/*
 * Get the layout slot for a specific tag on a monitor.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @return          Layout slot (0 or 1)
 */
int pertag_get_sellt(const struct Monitor* monitor, int tag);

/*
 * Pertag modifier functions
 */

/*
 * Set the master factor for a specific tag.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @param mfact    Master factor (0.0 - 1.0)
 */
void pertag_set_mfact(struct Monitor* monitor, int tag, float mfact);

/*
 * Set the number of masters for a specific tag.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @param nmaster  Number of masters
 */
void pertag_set_nmaster(struct Monitor* monitor, int tag, int nmaster);

/*
 * Set the layout slot for a specific tag.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @param sellt    Layout slot (0 or 1)
 */
void pertag_set_sellt(struct Monitor* monitor, int tag, int sellt);

/*
 * Tag switching functions
 */

/*
 * Switch to viewing a specific tag.
 * Saves current state to prevtag, restores state for new tag.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 */
void pertag_view(struct Monitor* monitor, int tag);

/*
 * Toggle to the previous tag.
 * @param monitor  Monitor pointer
 */
void pertag_toggle_prevtag(struct Monitor* monitor);

/*
 * Update focused window for current tag.
 * @param monitor  Monitor pointer
 * @param client_id  TargetID of focused client, or TARGET_ID_NONE
 */
void pertag_set_focused(struct Monitor* monitor, TargetID client_id);

/*
 * Get the focused window TargetID for a specific tag.
 * @param monitor  Monitor pointer
 * @param tag      Tag index (0-8)
 * @return          TargetID of focused client, or TARGET_ID_NONE
 */
TargetID pertag_get_focused(const struct Monitor* monitor, int tag);

/*
 * Pertag helper functions
 */

/*
 * Get or create Pertag data for a monitor.
 * Looks up in monitor's SM storage by "pertag" key.
 * @param monitor  Monitor pointer
 * @return         Pertag data or NULL
 */
Pertag* pertag_get_data(const struct Monitor* monitor);

/*
 * Check if a monitor has a valid pertag component.
 * @param monitor  Monitor pointer
 * @return         true if pertag data exists
 */
bool pertag_has_data(const struct Monitor* monitor);

/*
 * Initialize default values for a pertag state.
 * @param pt  Pertag structure to initialize
 * @param m  Monitor that owns this pertag
 */
void pertag_init_defaults(Pertag* pt, struct Monitor* m);

/*
 * Reset pertag state for a monitor (clear all per-tag data).
 * @param monitor  Monitor pointer
 */
void pertag_reset(struct Monitor* monitor);

#endif /* _PERTAG_H_ */
