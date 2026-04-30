/*
 * Monitor Manager Component Implementation
 *
 * Handles display detection via RandR.
 * - Registers XCB handler for RANDR_NOTIFY events
 * - Creates/destroys Monitor targets based on output connect/disconnect
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../target/monitor.h"
#include "../xcb/xcb-handler.h"
#include "monitor-manager.h"
#include "wm-hub.h"
#include "wm-log.h"

/* Forward declarations */
static void monitor_manager_executor(struct HubRequest* req);
static void monitor_manager_xcb_handler(void* event);

/*
 * Request types handled by monitor manager (if any)
 * Currently no requests are handled - all monitor state changes come from
 * XCB RandR events via raw writes.
 */
static RequestType monitor_manager_requests[] = { 0 }; /* 0-terminated */

/*
 * Target types accepted by monitor manager
 */
static TargetType monitor_manager_targets[] = {
  TARGET_TYPE_MONITOR,
  TARGET_TYPE_NONE,
};

/*
 * Monitor Manager Component
 */
HubComponent monitor_manager_component = {
  .name       = "monitor-manager",
  .requests   = monitor_manager_requests,
  .targets    = monitor_manager_targets,
  .executor   = monitor_manager_executor,
  .registered = false,
};

/*
 * Get the monitor manager component.
 */
HubComponent*
monitor_manager_get_component(void)
{
  return &monitor_manager_component;
}

/*
 * Initialize the monitor manager.
 * - Registers XCB handler for RANDR events
 * - Queries existing RandR outputs and creates Monitor targets
 *
 * This function is idempotent - multiple calls are safe.
 */
void
monitor_manager_init(void)
{
  LOG_INFO("Initializing monitor manager component");

  /* Register with hub (idempotent - no-op if already registered) */
  hub_register_component(&monitor_manager_component);

  /* Register XCB handler for RandR notify events.
   *
   * RandR events are extension events. The response_type sent by the X
   * server for RandR events is the base event type from
   * xcb_randr_get_selected_output_reply()->your_event_mask.
   * We register for XCB_RANDR_NOTIFY which is defined as value 1 in
   * xcb/randr.h.
   *
   * Note: RandR output discovery and initial Monitor creation would
   * be done here in a full implementation with working RandR support.
   */
  int result = xcb_handler_register(
      XCB_RANDR_NOTIFY,
      &monitor_manager_component,
      monitor_manager_xcb_handler);

  if (result != 0) {
    LOG_ERROR("Failed to register RandR handler");
    return;
  }

  LOG_INFO("Monitor manager component initialized");
}

/*
 * Shutdown the monitor manager.
 */
void
monitor_manager_shutdown(void)
{
  LOG_INFO("Shutting down monitor manager component");

  /* Unregister XCB handlers */
  xcb_handler_unregister_component(&monitor_manager_component);

  /* Note: We don't destroy monitors here - that happens via
   * monitor_list_shutdown() which is called by the main wm shutdown.
   */

  /* Unregister from hub */
  hub_unregister_component("monitor-manager");

  LOG_INFO("Monitor manager component shut down");
}

/*
 * Executor for monitor manager requests.
 * Currently, monitor manager doesn't handle any requests -
 * it only reacts to XCB RandR events via raw writes.
 */
static void
monitor_manager_executor(struct HubRequest* req)
{
  if (req == NULL)
    return;

  LOG_DEBUG("Monitor manager received request type=%u, target=%" PRIu64,
            req->type, req->target);

  /* Currently no requests are handled by monitor manager.
   * All monitor state changes come from XCB RandR events.
   */
  LOG_DEBUG("Monitor manager: no handler for request type=%u", req->type);
}

/*
 * Handle a RandR notify event.
 * Called when RandR notifies us of output changes.
 *
 * For a full implementation, this would:
 * 1. Parse the RandR event to determine which output changed
 * 2. On connect: create Monitor target, register with hub
 * 3. On disconnect: destroy Monitor target, unregister from hub
 */
void
monitor_manager_handle_randr_notify(void* event)
{
  if (event == NULL)
    return;

  /* RandR events are extension events. The response_type for RandR
   * notify events is XCB_RANDR_NOTIFY. The event subtype (connected,
   * disconnected, etc.) is encoded in the event structure.
   *
   * For a full implementation, we would parse the RandR event:
   * - xcb_randr_screen_change_notify_event_t for screen changes
   * - xcb_randr_output_change_notify_event_t for output changes
   * - xcb_randr_crtc_change_notify_event_t for crtc changes
   *
   * This handler is called by xcb_handler_dispatch() for all RandR
   * events. The actual event type is in the first byte after response_type.
   */

  /* Extract event subtype (first byte after response_type) */
  uint8_t* event_bytes   = (uint8_t*) event;
  uint8_t  randr_subtype = event_bytes[1];

  LOG_DEBUG("Monitor manager: RandR notify event, subtype=%u", randr_subtype);

  /* RandR notify subtypes:
   * 0 = RRNotify (base type)
   * 1 = RRNotify_CrtcChange
   * 2 = RRNotify_OutputChange
   * 3 = RRNotify_OutputProperty
   * 4 = RRNotify_ProviderChange
   * 5 = RRNotify_ProviderProperty
   * 6 = RRNotify_ResourceChange
   * 7 = RRNotify_AnchorGridNotify
   */

  switch (randr_subtype) {
  case 2: /* RRNotify_OutputChange - output connected/disconnected */
    LOG_DEBUG("Monitor manager: Output change notification");
    /* Parse output change event and create/destroy Monitor */
    break;

  case 1: /* RRNotify_CrtcChange - crtc configuration changed */
    LOG_DEBUG("Monitor manager: CRTC change notification");
    /* Update monitor geometry based on CRTC change */
    break;

  case 6: /* RRNotify_ResourceChange - resource changes */
    LOG_DEBUG("Monitor manager: Resource change notification");
    /* Handle resource changes */
    break;

  default:
    LOG_DEBUG("Monitor manager: Unhandled RandR subtype: %u", randr_subtype);
    break;
  }
}

/*
 * Internal XCB handler dispatcher wrapper.
 * Called by xcb_handler_dispatch() for RandR events.
 */
static void
monitor_manager_xcb_handler(void* event)
{
  monitor_manager_handle_randr_notify(event);
}
