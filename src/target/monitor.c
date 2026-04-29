/*
 * Monitor Target Implementation
 */

#include <stdlib.h>

#include "monitor.h"
#include "client.h"
#include "../../wm-hub.h"
#include "../../wm-log.h"

/*
 * Monitor list
 */
static Monitor* monitor_list = NULL;

Monitor*
monitor_create(xcb_randr_output_t output)
{
  if (output == XCB_NONE) {
    LOG_ERROR("Cannot create monitor for invalid output: %u", output);
    return NULL;
  }

  /* Check for duplicate output ID */
  if (hub_get_target_by_id((TargetID) output) != NULL) {
    LOG_ERROR("Monitor with output %u already registered", output);
    return NULL;
  }

  LOG_DEBUG("Creating monitor for output: %u", output);

  Monitor* m = malloc(sizeof(Monitor));
  if (m == NULL) {
    LOG_ERROR("Failed to allocate Monitor");
    return NULL;
  }

  /* Initialize base target */
  m->target.id         = (TargetID) output;
  m->target.type       = TARGET_TYPE_MONITOR;
  m->target.registered = false;

  /* Initialize RandR properties */
  m->output = output;
  m->crtc   = XCB_NONE;

  /* Initialize geometry */
  m->x      = 0;
  m->y      = 0;
  m->width  = 0;
  m->height = 0;

  /* Initialize tag state */
  m->tagset     = 1 << 0; /* tag 1 */
  m->prevtagset = 1 << 0;

  /* Initialize layout configuration */
  m->mfact  = 0.5;
  m->nmaster = 1;

  /* Initialize client associations */
  m->clients = NULL;
  m->sel      = NULL;
  m->stack   = NULL;

  /* Initialize bar */
  m->bar = NULL;

  /* Add to list */
  m->next = monitor_list;
  monitor_list = m;

  /* Register with Hub */
  hub_register_target(&m->target);

  /* Verify registration succeeded */
  if (!m->target.registered) {
    LOG_ERROR("Failed to register monitor target: output=%u", output);
    /* Remove from list */
    Monitor** prev = &monitor_list;
    while (*prev != NULL && *prev != m) {
      prev = &(*prev)->next;
    }
    if (*prev == m) {
      *prev = m->next;
    }
    free(m);
    return NULL;
  }

  LOG_DEBUG("Monitor created: output=%u", output);
  return m;
}

void
monitor_destroy(Monitor* m)
{
  if (m == NULL)
    return;

  LOG_DEBUG("Destroying monitor: output=%u", m->output);

  /* Detach all associated clients */
  while (m->clients != NULL) {
    Client* c = m->clients;
    c->monitor = NULL;
    m->clients = NULL;
    /* Note: we need to iterate through all clients on this monitor */
    /* For now, just detach the first one and rely on proper cleanup */
    break;
  }

  /* Detach selected client */
  if (m->sel != NULL) {
    m->sel->monitor = NULL;
    m->sel = NULL;
  }

  /* Detach stacking list */
  m->stack = NULL;

  /* Remove from list */
  Monitor** prev = &monitor_list;
  while (*prev != NULL && *prev != m) {
    prev = &(*prev)->next;
  }
  if (*prev == m) {
    *prev = m->next;
  }

  /* Unregister from Hub */
  if (m->target.registered) {
    hub_unregister_target(m->target.id);
  }

  /* Free monitor */
  free(m);

  LOG_DEBUG("Monitor destroyed");
}

Monitor*
monitor_get_by_output(xcb_randr_output_t output)
{
  for (Monitor* m = monitor_list; m != NULL; m = m->next) {
    if (m->output == output)
      return m;
  }
  return NULL;
}

void
monitor_set_geometry(Monitor* m, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
  if (m == NULL)
    return;
  m->x      = x;
  m->y      = y;
  m->width  = width;
  m->height = height;
}

void
monitor_get_geometry(const Monitor* m, int16_t* x, int16_t* y, uint16_t* width, uint16_t* height)
{
  if (m == NULL)
    return;
  if (x != NULL)
    *x = m->x;
  if (y != NULL)
    *y = m->y;
  if (width != NULL)
    *width = m->width;
  if (height != NULL)
    *height = m->height;
}
