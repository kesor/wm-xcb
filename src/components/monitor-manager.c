/*
 * Monitor Manager Component Implementation
 *
 * Handles display detection via RandR.
 * - Registers XCB handler for RANDR_NOTIFY events
 * - Creates/destroys Monitor targets based on output connect/disconnect
 * - Performs initial RandR output discovery on startup
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/randr.h>

#include "../target/monitor.h"
#include "../xcb/xcb-handler.h"
#include "monitor-manager.h"
#include "wm-hub.h"
#include "wm-log.h"

/* External XCB connection - declared in wm-xcb.h */
extern xcb_connection_t* dpy;

/*
 * RandR constants for connection state
 * (these are defined in X protocol, not xcb/randr.h)
 */
#define RANDR_CONNECTED    0
#define RANDR_DISCONNECTED 1
#define RANDR_UNKNOWN      2

/* Forward declarations */
static void monitor_manager_executor(struct HubRequest* req);
static void monitor_manager_xcb_handler(void* event);

/* Internal helper functions */
static void                monitor_manager_discover_outputs(void);
static Monitor*            monitor_manager_create_for_output(xcb_randr_output_t output);
static void                monitor_manager_destroy_for_output(xcb_randr_output_t output);
static void                monitor_manager_update_output_geometry(xcb_randr_output_t output, xcb_randr_crtc_t crtc);
static void                monitor_manager_update_crtc_geometry(xcb_randr_crtc_t crtc, xcb_timestamp_t timestamp);
static bool                monitor_manager_get_output_info(xcb_randr_output_t output, xcb_randr_crtc_t* crtc_out, char** name_out);
static bool                monitor_manager_get_crtc_info(xcb_randr_crtc_t crtc, int16_t* x, int16_t* y, uint16_t* width, uint16_t* height, xcb_randr_mode_t* mode);
static xcb_randr_output_t* monitor_manager_get_screen_outputs(xcb_window_t root, int* count);

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
   * server for RandR events is XCB_RANDR_NOTIFY (value 1).
   *
   * Note: RandR output discovery and initial Monitor creation from existing
   * outputs is not yet implemented - requires working RandR support.
   */
  int result = xcb_handler_register(
      XCB_RANDR_NOTIFY,
      &monitor_manager_component,
      monitor_manager_xcb_handler);

  if (result != 0) {
    LOG_ERROR("Failed to register RandR handler");
    return;
  }

  /* Select RandR notify events on the root window so we receive them.
   * Without this, the X server won't deliver RandR notify events to us.
   * We select for all notify types we handle: output change, CRTC change,
   * and resource change. Provider/lease events are not selected.
   */
  extern xcb_window_t root;
  if (dpy != NULL && root != XCB_NONE) {
    uint32_t notify_mask =
        XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
        XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
        XCB_RANDR_NOTIFY_MASK_RESOURCE_CHANGE;
    xcb_randr_select_input(dpy, root, notify_mask);
    xcb_flush(dpy);
  } else {
    LOG_DEBUG("Monitor manager: Cannot select RandR input - no X connection");
  }

  /* Discover existing monitors via RandR
   * This queries the X server for all currently connected outputs
   * and creates Monitor targets for each one.
   */
  monitor_manager_discover_outputs();

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
 */
void
monitor_manager_handle_randr_notify(void* event)
{
  if (event == NULL)
    return;

  /* Cast to the RandR notify event structure */
  xcb_randr_notify_event_t* randr_event = (xcb_randr_notify_event_t*) event;

  LOG_DEBUG("Monitor manager: RandR notify event, subtype=%u", randr_event->subCode);

  switch (randr_event->subCode) {
  case XCB_RANDR_NOTIFY_OUTPUT_CHANGE: {
    /* Output connection/disconnection event */
    xcb_randr_output_change_t* oc = &randr_event->u.oc;
    LOG_DEBUG("Monitor manager: Output change - output=%u, crtc=%u, connection=%u",
              oc->output, oc->crtc, oc->connection);

    if (oc->connection == XCB_RANDR_CONNECTION_DISCONNECTED) {
      /* Output disconnected - destroy monitor */
      monitor_manager_destroy_for_output(oc->output);
    } else if (oc->connection == XCB_RANDR_CONNECTION_CONNECTED) {
      /* Output connected - create monitor if not exists */
      if (monitor_get_by_output(oc->output) == NULL) {
        monitor_manager_create_for_output(oc->output);
      }
      /* Update geometry from the CRTC */
      if (oc->crtc != XCB_NONE) {
        monitor_manager_update_output_geometry(oc->output, oc->crtc);
      }
    }
    break;
  }

  case XCB_RANDR_NOTIFY_CRTC_CHANGE: {
    /* CRTC configuration changed - update affected output's monitor */
    xcb_randr_crtc_change_t* cc = &randr_event->u.cc;
    LOG_DEBUG("Monitor manager: CRTC change - crtc=%u, mode=%u, x=%d y=%d %ux%u",
              cc->crtc, cc->mode, cc->x, cc->y, cc->width, cc->height);

    /* Find the output that uses this CRTC and update its geometry */
    monitor_manager_update_crtc_geometry(cc->crtc, cc->timestamp);
    break;
  }

  case XCB_RANDR_NOTIFY_RESOURCE_CHANGE: {
    /* Resource change - refresh output list to detect changes */
    LOG_DEBUG("Monitor manager: Resource change notification");
    /* Re-discover outputs to sync our state */
    monitor_manager_discover_outputs();
    break;
  }

  case XCB_RANDR_NOTIFY_OUTPUT_PROPERTY:
  case XCB_RANDR_NOTIFY_PROVIDER_CHANGE:
  case XCB_RANDR_NOTIFY_PROVIDER_PROPERTY:
  case XCB_RANDR_NOTIFY_LEASE:
    /* These events don't require immediate action for basic monitor management */
    LOG_DEBUG("Monitor manager: RandR event subtype %u (no action needed)",
              randr_event->subCode);
    break;

  default:
    LOG_DEBUG("Monitor manager: Unknown RandR subtype: %u", randr_event->subCode);
    break;
  }
}

/*
 * Discover all currently connected outputs via RandR.
 * Creates Monitor targets for each connected output.
 *
 * Note: This function requires a valid X connection (dpy != NULL)
 * and root window. In test environments without X, this is a no-op.
 */
static void
monitor_manager_discover_outputs(void)
{
  /* Check for valid X connection - skip discovery in test environment */
  if (dpy == NULL) {
    LOG_DEBUG("Monitor manager: No X connection, skipping output discovery");
    return;
  }

  extern xcb_window_t root;

  /* Skip if root window is not set */
  if (root == XCB_NONE) {
    LOG_DEBUG("Monitor manager: Root window not set, skipping output discovery");
    return;
  }

  int                 output_count = 0;
  xcb_randr_output_t* outputs      = monitor_manager_get_screen_outputs(root, &output_count);

  if (outputs == NULL || output_count == 0) {
    LOG_DEBUG("Monitor manager: No RandR outputs found");
    return;
  }

  LOG_DEBUG("Monitor manager: Discovering %d RandR outputs", output_count);

  for (int i = 0; i < output_count; i++) {
    xcb_randr_output_t output = outputs[i];

    /* Query output info once - get connection state and CRTC */
    xcb_randr_get_output_info_cookie_t cookie = xcb_randr_get_output_info(dpy, output, XCB_CURRENT_TIME);
    xcb_randr_get_output_info_reply_t* reply  = xcb_randr_get_output_info_reply(dpy, cookie, NULL);

    if (reply == NULL) {
      continue;
    }

    /* Only create monitor for connected outputs with a valid CRTC */
    if (reply->crtc != XCB_NONE && reply->connection == XCB_RANDR_CONNECTION_CONNECTED) {
      /* Only create if monitor doesn't already exist */
      if (monitor_get_by_output(output) == NULL) {
        Monitor* m = monitor_manager_create_for_output(output);
        if (m != NULL) {
          monitor_manager_update_output_geometry(output, reply->crtc);
        }
      }
    }

    free(reply);
  }

  free(outputs);
}

/*
 * Get list of outputs for the root window.
 * Returns newly allocated array (caller must free) or NULL on error.
 * Output count is stored in *count.
 *
 * Note: Caller should check dpy != NULL before calling.
 */
static xcb_randr_output_t*
monitor_manager_get_screen_outputs(xcb_window_t root, int* count)
{
  /* Check for valid X connection */
  if (dpy == NULL) {
    *count = 0;
    return NULL;
  }

  xcb_randr_get_screen_resources_cookie_t cookie = xcb_randr_get_screen_resources(dpy, root);
  xcb_randr_get_screen_resources_reply_t* reply  = xcb_randr_get_screen_resources_reply(dpy, cookie, NULL);

  if (reply == NULL) {
    *count = 0;
    return NULL;
  }

  xcb_randr_output_t* outputs     = NULL;
  int                 outputs_len = 0;

  /* Get the outputs array from the reply */
  xcb_randr_output_t* reply_outputs     = xcb_randr_get_screen_resources_outputs(reply);
  int                 reply_outputs_len = xcb_randr_get_screen_resources_outputs_length(reply);

  if (reply_outputs_len > 0) {
    outputs_len = reply_outputs_len;
    outputs     = malloc((size_t) outputs_len * sizeof(xcb_randr_output_t));
    if (outputs != NULL) {
      memcpy(outputs, reply_outputs, (size_t) outputs_len * sizeof(xcb_randr_output_t));
    } else {
      outputs_len = 0;
    }
  }

  free(reply);
  *count = outputs_len;
  return outputs;
}

/*
 * Create a Monitor for a RandR output.
 * Returns the created Monitor or NULL on failure.
 */
static Monitor*
monitor_manager_create_for_output(xcb_randr_output_t output)
{
  if (output == XCB_NONE) {
    return NULL;
  }

  /* Check if monitor already exists */
  if (monitor_get_by_output(output) != NULL) {
    LOG_DEBUG("Monitor for output %u already exists", output);
    return monitor_get_by_output(output);
  }

  LOG_INFO("Monitor manager: Creating monitor for output %u", output);

  Monitor* m = monitor_create(output);
  if (m != NULL) {
    /* Store the connection state machine for this monitor */
    /* Note: connection SM is created by the monitor component when it adopts */
  }

  return m;
}

/*
 * Destroy the Monitor for a RandR output.
 */
static void
monitor_manager_destroy_for_output(xcb_randr_output_t output)
{
  Monitor* m = monitor_get_by_output(output);
  if (m == NULL) {
    LOG_DEBUG("Monitor for output %u not found (already destroyed or not managed)", output);
    return;
  }

  LOG_INFO("Monitor manager: Destroying monitor for output %u", output);

  /* Unregister from hub before destroying - monitor_destroy() also unregisters
   * but we do it here to ensure the target is removed before we free the Monitor.
   * monitor_destroy() will skip unregistration if already unregistered.
   */
  if (m->target.registered) {
    hub_unregister_target(m->target.id);
  }
  monitor_destroy(m);
}

/*
 * Update monitor geometry based on CRTC assignment.
 * Finds the monitor for the given output and updates its geometry.
 */
static void
monitor_manager_update_output_geometry(xcb_randr_output_t output, xcb_randr_crtc_t crtc)
{
  Monitor* m = monitor_get_by_output(output);
  if (m == NULL) {
    return;
  }

  /* Update the stored CRTC */
  m->crtc = crtc;

  /* Get CRTC geometry */
  int16_t          x      = 0;
  int16_t          y      = 0;
  uint16_t         width  = 0;
  uint16_t         height = 0;
  xcb_randr_mode_t mode   = 0;

  if (monitor_manager_get_crtc_info(crtc, &x, &y, &width, &height, &mode)) {
    monitor_set_geometry(m, x, y, width, height);
    LOG_DEBUG("Monitor manager: Updated output %u geometry to %ux%u+%d+%d",
              output, (unsigned int) width, (unsigned int) height, x, y);
  }
}

/*
 * Update monitor geometry for all outputs using a specific CRTC.
 * Called when CRTC configuration changes.
 *
 * Note: Requires valid X connection (dpy != NULL).
 */
static void
monitor_manager_update_crtc_geometry(xcb_randr_crtc_t crtc, xcb_timestamp_t timestamp)
{
  /* Check for valid X connection */
  if (dpy == NULL) {
    return;
  }

  extern xcb_window_t root;

  /* Find the output that uses this CRTC */
  int                 output_count = 0;
  xcb_randr_output_t* outputs      = monitor_manager_get_screen_outputs(root, &output_count);

  if (outputs == NULL) {
    return;
  }

  for (int i = 0; i < output_count; i++) {
    xcb_randr_get_output_info_cookie_t cookie = xcb_randr_get_output_info(dpy, outputs[i], timestamp);
    xcb_randr_get_output_info_reply_t* reply  = xcb_randr_get_output_info_reply(dpy, cookie, NULL);

    if (reply != NULL && reply->crtc == crtc) {
      Monitor* m = monitor_get_by_output(outputs[i]);
      if (m != NULL) {
        monitor_manager_update_output_geometry(outputs[i], crtc);
      }
    }

    free(reply);
  }

  free(outputs);
}

/*
 * Get information about a RandR output.
 * Fills crtc_out with the current CRTC and optionally name_out with the output name.
 * Returns true on success, false on failure.
 *
 * Note: Caller should check dpy != NULL before calling.
 */
static bool
monitor_manager_get_output_info(xcb_randr_output_t output, xcb_randr_crtc_t* crtc_out, char** name_out)
{
  /* Check for valid X connection */
  if (dpy == NULL) {
    return false;
  }

  xcb_randr_get_output_info_cookie_t cookie = xcb_randr_get_output_info(dpy, output, XCB_CURRENT_TIME);
  xcb_randr_get_output_info_reply_t* reply  = xcb_randr_get_output_info_reply(dpy, cookie, NULL);

  if (reply == NULL) {
    return false;
  }

  if (crtc_out != NULL) {
    *crtc_out = reply->crtc;
  }

  if (name_out != NULL) {
    int   name_len = xcb_randr_get_output_info_name_length(reply);
    char* name     = malloc((size_t) name_len + 1);
    if (name != NULL) {
      memcpy(name, xcb_randr_get_output_info_name(reply), (size_t) name_len);
      name[name_len] = '\0';
      *name_out      = name;
    }
  }

  free(reply);
  return true;
}

/*
 * Get information about a RandR CRTC.
 * Fills x, y, width, height, mode with CRTC configuration.
 * Returns true on success, false on failure.
 *
 * Note: Caller should check dpy != NULL and crtc != XCB_NONE before calling.
 */
static bool
monitor_manager_get_crtc_info(xcb_randr_crtc_t crtc, int16_t* x, int16_t* y,
                              uint16_t* width, uint16_t* height, xcb_randr_mode_t* mode)
{
  /* Check for valid X connection */
  if (dpy == NULL) {
    return false;
  }

  if (crtc == XCB_NONE) {
    return false;
  }

  xcb_randr_get_crtc_info_cookie_t cookie = xcb_randr_get_crtc_info(dpy, crtc, XCB_CURRENT_TIME);
  xcb_randr_get_crtc_info_reply_t* reply  = xcb_randr_get_crtc_info_reply(dpy, cookie, NULL);

  if (reply == NULL) {
    return false;
  }

  if (x != NULL)
    *x = reply->x;
  if (y != NULL)
    *y = reply->y;
  if (width != NULL)
    *width = reply->width;
  if (height != NULL)
    *height = reply->height;
  if (mode != NULL)
    *mode = reply->mode;

  free(reply);
  return true;
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