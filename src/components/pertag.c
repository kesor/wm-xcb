/*
 * Pertag Component - Per-Tag State Management for Monitors
 *
 * Bridges MONITOR → TAG targets by storing per-tag state.
 * When a monitor switches between tags, saves/restores monitor state.
 *
 * Design: Pertag allocates and owns its own data, stored in the monitor's
 * SM storage by name "pertag". This keeps Monitor decoupled from pertag.
 */

#include <stdlib.h>
#include <string.h>

#include "../target/monitor.h"
#include "../target/tag.h"
#include "pertag.h"
#include "wm-hub.h"
#include "wm-log.h"

/*
 * Name used to store Pertag data in monitor's SM storage
 */
#define PERTAG_SM_NAME "pertag"

/*
 * Pertag component - registered with hub for adoption
 */
static RequestType  pertag_requests[] = { 0 }; /* No requests */
static TargetType   pertag_targets[]  = { TARGET_TYPE_MONITOR, TARGET_TYPE_NONE };
static HubComponent pertag_component  = {
   .name       = "pertag",
   .requests   = pertag_requests,
   .targets    = pertag_targets,
   .executor   = NULL, /* No requests */
   .on_adopt   = pertag_on_adopt,
   .on_unadopt = pertag_on_unadopt,
   .registered = false,
};

/*
 * Pertag component lifecycle
 */

/*
 * Initialize the pertag component.
 * Registers with hub so it can be adopted by monitors.
 */
void
pertag_component_init(void)
{
  if (pertag_component.registered) {
    LOG_DEBUG("Pertag component already registered");
    return;
  }

  hub_register_component(&pertag_component);
  LOG_DEBUG("Pertag component registered with hub");
}

/*
 * Shutdown the pertag component.
 */
void
pertag_component_shutdown(void)
{
  if (!pertag_component.registered) {
    return;
  }

  hub_unregister_component("pertag");
  LOG_DEBUG("Pertag component unregistered from hub");
}

/*
 * Initialize default values for a pertag state.
 */
static void
pertag_init_internal(Pertag* pt, struct Monitor* m)
{
  if (pt == NULL)
    return;

  pt->monitor = m;
  pt->curtag  = 1; /* Default to tag 1 (index 1, mask = 1 << 1 = 2) */
  pt->prevtag = 0; /* No previous tag initially */

  /* Initialize arrays with defaults */
  for (int i = 0; i <= PERTAG_NUM_TAGS; i++) {
    pt->mfacts[i]   = 0.5F; /* Default master factor */
    pt->nmasters[i] = 1;    /* Default nmaster */
    pt->sellts[i]   = 0;    /* Default layout slot */
    pt->showbars[i] = true; /* Bar visible by default */
    pt->focused[i]  = TARGET_ID_NONE;

    /* Layouts default to NULL (will be set by tiling component) */
    pt->layouts[i][0] = NULL;
    pt->layouts[i][1] = NULL;
  }

  /* Index 0 is "all tags" - copy defaults from index 1 */
  pt->mfacts[0]   = pt->mfacts[1];
  pt->nmasters[0] = pt->nmasters[1];
  pt->sellts[0]   = pt->sellts[1];
}

/*
 * Initialize the pertag component for a monitor.
 * Allocates Pertag data and stores it in the monitor's SM storage.
 */
void
pertag_on_adopt(HubTarget* target)
{
  if (target == NULL || target->type != TARGET_TYPE_MONITOR) {
    LOG_ERROR("pertag_on_adopt: invalid target");
    return;
  }

  Monitor* m = (Monitor*) target;

  /* Check if pertag already exists */
  if (pertag_has_data(m)) {
    LOG_DEBUG("Pertag already exists for monitor");
    return;
  }

  /* Allocate Pertag data */
  Pertag* pt = malloc(sizeof(Pertag));
  if (pt == NULL) {
    LOG_ERROR("Failed to allocate Pertag for monitor");
    return;
  }

  /* Initialize with defaults */
  pertag_init_internal(pt, m);

  /* Store in monitor's SM storage */
  if (!monitor_set_sm(m, PERTAG_SM_NAME, (StateMachine*) pt)) {
    LOG_ERROR("Failed to store Pertag in monitor SM storage");
    free(pt);
    return;
  }

  LOG_DEBUG("Pertag component adopted by monitor: %lu", (unsigned long) target->id);
}

/*
 * Cleanup pertag component data.
 * Frees the Pertag data stored in monitor's SM storage.
 */
void
pertag_on_unadopt(HubTarget* target)
{
  if (target == NULL || target->type != TARGET_TYPE_MONITOR)
    return;

  Monitor* m  = (Monitor*) target;
  Pertag*  pt = pertag_get_data(m);

  if (pt == NULL)
    return;

  /* Clear from monitor's SM storage (this destroys the "SM") */
  monitor_set_sm(m, PERTAG_SM_NAME, NULL);

  /* Free Pertag data */
  free(pt);

  LOG_DEBUG("Pertag component unadopted by monitor: %lu", (unsigned long) target->id);
}

/*
 * Get Pertag data for a monitor.
 */
Pertag*
pertag_get_data(const struct Monitor* monitor)
{
  if (monitor == NULL)
    return NULL;

  /* Look up in monitor's SM storage by name */
  StateMachine* sm = monitor_get_sm((Monitor*) monitor, PERTAG_SM_NAME);
  if (sm == NULL)
    return NULL;

  return (Pertag*) sm;
}

/*
 * Check if a monitor has a valid pertag component.
 */
bool
pertag_has_data(const struct Monitor* monitor)
{
  return pertag_get_data(monitor) != NULL;
}

/*
 * Get the current tag index for a monitor.
 */
int
pertag_get_curtag(const struct Monitor* monitor)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return 1; /* Default to tag 1 */
  return pt->curtag;
}

/*
 * Get the previous tag index for a monitor.
 */
int
pertag_get_prevtag(const struct Monitor* monitor)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return 0;
  return pt->prevtag;
}

/*
 * Get the master factor for a specific tag on a monitor.
 */
float
pertag_get_mfact(const struct Monitor* monitor, int tag)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return 0.5F;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  return pt->mfacts[tag];
}

/*
 * Get the number of masters for a specific tag on a monitor.
 */
int
pertag_get_nmaster(const struct Monitor* monitor, int tag)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return 1;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  return pt->nmasters[tag];
}

/*
 * Get the layout slot for a specific tag on a monitor.
 */
int
pertag_get_sellt(const struct Monitor* monitor, int tag)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return 0;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  return pt->sellts[tag];
}

/*
 * Set the master factor for a specific tag.
 */
void
pertag_set_mfact(struct Monitor* monitor, int tag, float mfact)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  /* Clamp to valid range */
  if (mfact < 0.0F)
    mfact = 0.0F;
  if (mfact > 1.0F)
    mfact = 1.0F;

  pt->mfacts[tag] = mfact;

  /* Update index 0 as well (all tags) */
  if (tag > 0)
    pt->mfacts[0] = mfact;
}

/*
 * Set the number of masters for a specific tag.
 */
void
pertag_set_nmaster(struct Monitor* monitor, int tag, int nmaster)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  /* Clamp to non-negative */
  if (nmaster < 0)
    nmaster = 0;

  pt->nmasters[tag] = nmaster;

  /* Update index 0 as well */
  if (tag > 0)
    pt->nmasters[0] = nmaster;
}

/*
 * Set the layout slot for a specific tag.
 */
void
pertag_set_sellt(struct Monitor* monitor, int tag, int sellt)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  /* Only allow 0 or 1 */
  sellt = sellt ? 1 : 0;

  pt->sellts[tag] = sellt;

  /* Update index 0 as well */
  if (tag > 0)
    pt->sellts[0] = sellt;
}

/*
 * Switch to viewing a specific tag.
 * Saves current state to prevtag, restores state for new tag.
 */
void
pertag_view(struct Monitor* monitor, int tag)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL) {
    LOG_WARN("pertag_view: no pertag data for monitor");
    return;
  }

  if (tag < 0 || tag > PERTAG_NUM_TAGS) {
    LOG_WARN("pertag_view: invalid tag %d", tag);
    return;
  }

  /* Save current state to prevtag */
  int old_tag = pt->curtag;
  if (old_tag != tag) {
    pt->prevtag = old_tag;

    /* Save current settings
     * Note: Layout is stored by the tiling component
     * This just saves the settings we track here
     */
    LOG_DEBUG("Pertag saving tag %d state before switching to %d", old_tag, tag);
  }

  /* Restore state for new tag (or use defaults if first visit) */
  pt->curtag = tag;

  /* Restore settings from new tag's slot */
  LOG_DEBUG("Pertag restored tag %d state", tag);
}

/*
 * Toggle to the previous tag.
 */
void
pertag_toggle_prevtag(struct Monitor* monitor)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return;

  int prev = pt->prevtag;
  if (prev > 0 && prev <= PERTAG_NUM_TAGS) {
    pertag_view(monitor, prev);
  }
}

/*
 * Update focused window for current tag.
 */
void
pertag_set_focused(struct Monitor* monitor, TargetID client_id)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return;

  pt->focused[pt->curtag] = client_id;
}

/*
 * Get the focused window TargetID for a specific tag.
 */
TargetID
pertag_get_focused(const struct Monitor* monitor, int tag)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return TARGET_ID_NONE;

  if (tag < 0 || tag > PERTAG_NUM_TAGS)
    tag = 0;

  return pt->focused[tag];
}

/*
 * Reset pertag state for a monitor.
 */
void
pertag_reset(struct Monitor* monitor)
{
  Pertag* pt = pertag_get_data(monitor);
  if (pt == NULL)
    return;

  /* Reinitialize defaults */
  pertag_init_internal(pt, monitor);
}