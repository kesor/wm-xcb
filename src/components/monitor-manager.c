/*
 * Monitor Manager Component Implementation
 *
 * Handles display detection via RandR.
 * - Registers XCB handler for RANDR_NOTIFY events
 * - Creates/destroys Monitor targets based on output connect/disconnect
 * - Reads the geometry with a pass of unchecked queries whose replies the loop
 *   reads when they arrive; only the startup discovery waits
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/randr.h>
#include <xcb/xcbext.h>

#include "../target/monitor.h"
#include "../xcb/xcb-handler.h"
#include "monitor-manager.h"
#include "wm-hub.h"
#include "wm-log.h"
#include "wm-xcb.h"

/*
 * RandR constants for connection state
 * (these are defined in X protocol, not xcb/randr.h)
 */
#define RANDR_CONNECTED    0
#define RANDR_DISCONNECTED 1
#define RANDR_UNKNOWN      2

/* Set only once the handler is registered and the outputs are discovered */
static bool initialized = false;

/* Forward declarations */
static void monitor_manager_executor(struct HubRequest* req);
static void monitor_manager_xcb_handler(void* event);
static bool monitor_manager_pass_begin(void);
static void monitor_manager_pass_abort(void);
static void monitor_manager_pass_poll(void* userdata);
static void monitor_manager_apply_crtc_geometry(xcb_randr_crtc_t crtc, int16_t x, int16_t y, uint16_t width, uint16_t height);

/* Internal helper functions */
static void                monitor_manager_discover_outputs_blocking(void);
static Monitor*            monitor_manager_create_for_output(xcb_randr_output_t output);
static void                monitor_manager_destroy_for_output(xcb_randr_output_t output);
static xcb_randr_crtc_t    monitor_manager_apply_output_info(xcb_randr_output_t output, const xcb_randr_get_output_info_reply_t* reply);
static void                monitor_manager_apply_crtc_info(xcb_randr_output_t output, const xcb_randr_get_crtc_info_reply_t* reply);
static xcb_randr_output_t* monitor_manager_read_screen_outputs_blocking(xcb_window_t root, int* count);

/*
 * Request types handled by monitor manager (if any)
 * Currently no requests are handled - all monitor state changes come from
 * XCB RandR events via raw writes.
 */
static RequestType monitor_manager_requests[] = { 0 }; /* 0-terminated */

/*
 * Target type names accepted by monitor manager
 */
static const char* monitor_manager_target_names[] = {
  "monitor",
  NULL,
};

/*
 * Monitor Manager Component
 */
HubComponent monitor_manager_component = {
  .name                  = MONITOR_MANAGER_COMPONENT_NAME,
  .requests              = monitor_manager_requests,
  .accepted_target_names = monitor_manager_target_names,
  .accepted_targets      = NULL,
  .executor              = monitor_manager_executor,
  .registered            = false,
};

/*
 * Check if the component is initialized (for the startup check)
 */
bool
monitor_manager_is_initialized(void)
{
  return initialized;
}

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
  monitor_manager_discover_outputs_blocking();

  initialized = true;
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

  /* A reply still in flight is dropped rather than left for a caller that no
     longer exists. */
  monitor_manager_pass_abort();

  /* Note: We don't destroy monitors here - that happens via
   * monitor_list_shutdown() which is called by the main wm shutdown.
   */

  /* Unregister from hub */
  hub_unregister_component(MONITOR_MANAGER_COMPONENT_NAME);

  initialized = false;
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
 * What an output-info reply says about an output.
 *
 * An output that is connected and has a CRTC is a monitor the WM can place
 * clients on, and that CRTC's geometry is the next question the pass asks.
 * Returns the CRTC whose geometry has to be read, or XCB_NONE when there is
 * nothing to measure.
 */
static xcb_randr_crtc_t
monitor_manager_apply_output_info(xcb_randr_output_t output, const xcb_randr_get_output_info_reply_t* reply)
{
  if (reply == NULL)
    return XCB_NONE;

  if (reply->crtc == XCB_NONE || reply->connection != XCB_RANDR_CONNECTION_CONNECTED)
    return XCB_NONE;

  if (monitor_get_by_output(output) == NULL)
    monitor_manager_create_for_output(output);

  Monitor* m = monitor_get_by_output(output);
  if (m == NULL)
    return XCB_NONE;

  m->crtc = reply->crtc;
  return reply->crtc;
}

/*
 * Apply a CRTC geometry reply to the monitor of `output`.
 */
static void
monitor_manager_apply_crtc_info(xcb_randr_output_t output, const xcb_randr_get_crtc_info_reply_t* reply)
{
  if (reply == NULL)
    return;

  Monitor* m = monitor_get_by_output(output);
  if (m == NULL)
    return;

  monitor_set_geometry(m, reply->x, reply->y, reply->width, reply->height);
  LOG_DEBUG("Monitor manager: Updated output %u geometry to %ux%u+%d+%d",
            output, (unsigned int) reply->width, (unsigned int) reply->height,
            reply->x, reply->y);
}

/*
 * Apply the geometry a CRTC change event carries.
 *
 * The event holds the new x, y, width and height, so this needs no reply. Which
 * monitors the CRTC drives was recorded when their output info was read.
 */
static void
monitor_manager_apply_crtc_geometry(xcb_randr_crtc_t crtc, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
  for (Monitor* m = monitor_list_get_first(); m != NULL; m = monitor_list_get_next(m)) {
    if (m->crtc != crtc)
      continue;

    monitor_set_geometry(m, x, y, width, height);
    LOG_DEBUG("Monitor manager: CRTC %u is now %ux%u+%d+%d on output %u",
              crtc, (unsigned int) width, (unsigned int) height, x, y, m->output);
  }
}

/*
 * The RandR pass in flight.
 *
 * Finding out what the display looks like costs a chain of queries: the list of
 * outputs, then one output's info, then the geometry of the CRTC that output
 * drives. Each answer is a reply, which the loop does not deliver as an event,
 * so the pass keeps its place here and the loop's poll callback advances it one
 * step per iteration. A reply that has not arrived leaves the pass where it is;
 * nothing in the chain waits.
 */
typedef enum MonitorPass {
  MONITOR_PASS_NONE = 0,
  MONITOR_PASS_RESOURCES,   /* waiting for the list of outputs */
  MONITOR_PASS_OUTPUT_INFO, /* waiting for one output's info */
  MONITOR_PASS_CRTC_INFO,   /* waiting for the geometry of that output's CRTC */
} MonitorPass;

static struct {
  MonitorPass                             pass;
  xcb_randr_get_screen_resources_cookie_t resources;
  xcb_randr_get_output_info_cookie_t      output_info;
  xcb_randr_get_crtc_info_cookie_t        crtc_info;
  xcb_randr_output_t*                     outputs;
  int                                     output_count;
  int                                     output_index;
  xcb_randr_output_t                      output;
  xcb_timestamp_t                         timestamp;
  bool                                    again;   /* a change arrived while this pass ran */
  bool                                    polling; /* registered with the loop */
} randr_pass;

static void
monitor_manager_pass_remove_poll(void)
{
  if (!randr_pass.polling)
    return;

  wm_loop_remove_poll_handler(monitor_manager_pass_poll, NULL);
  randr_pass.polling = false;
}

static void
monitor_manager_pass_free_outputs(void)
{
  free(randr_pass.outputs);
  randr_pass.outputs      = NULL;
  randr_pass.output_count = 0;
  randr_pass.output_index = 0;
  randr_pass.pass         = MONITOR_PASS_NONE;
}

static void
monitor_manager_pass_issue_output(int index)
{
  randr_pass.output_index = index;
  randr_pass.output       = randr_pass.outputs[index];
  randr_pass.pass         = MONITOR_PASS_OUTPUT_INFO;
  randr_pass.output_info  = xcb_randr_get_output_info(dpy, randr_pass.output, randr_pass.timestamp);
}

/*
 * End of the pass, or the next output in it. A change that arrived while the
 * pass ran starts a fresh one: the list it walked is the one from before the
 * change.
 */
static void
monitor_manager_pass_finish(void)
{
  bool again = randr_pass.again;

  monitor_manager_pass_free_outputs();
  randr_pass.again = false;

  if (again) {
    (void) monitor_manager_pass_begin();
    return;
  }

  monitor_manager_pass_remove_poll();
}

static void
monitor_manager_pass_next_output(void)
{
  if (randr_pass.output_index + 1 < randr_pass.output_count) {
    monitor_manager_pass_issue_output(randr_pass.output_index + 1);
    return;
  }

  monitor_manager_pass_finish();
}

/*
 * Start a pass, or ask the one in flight to run again when it is done.
 *
 * One pass at a time: a second list would be read while the first is still
 * being walked, and the replies of the two would be crossed.
 */
static bool
monitor_manager_pass_begin(void)
{
  if (dpy == NULL || root == XCB_NONE)
    return false;

  if (randr_pass.pass != MONITOR_PASS_NONE) {
    randr_pass.again = true;
    return true;
  }

  if (!randr_pass.polling) {
    if (!wm_loop_add_poll_handler(monitor_manager_pass_poll, NULL)) {
      LOG_ERROR("Monitor manager: cannot read RandR, the loop refused the callback");
      return false;
    }
    randr_pass.polling = true;
  }

  randr_pass.timestamp = XCB_CURRENT_TIME;
  randr_pass.resources = xcb_randr_get_screen_resources(dpy, root);
  randr_pass.pass      = MONITOR_PASS_RESOURCES;
  return true;
}

/*
 * Drop the pass in flight. Its reply is never read, so libxcb is told to throw
 * it away instead of holding it for a caller that is gone.
 */
static void
monitor_manager_pass_abort(void)
{
  if (dpy != NULL) {
    switch (randr_pass.pass) {
    case MONITOR_PASS_RESOURCES:
      xcb_discard_reply(dpy, randr_pass.resources.sequence);
      break;
    case MONITOR_PASS_OUTPUT_INFO:
      xcb_discard_reply(dpy, randr_pass.output_info.sequence);
      break;
    case MONITOR_PASS_CRTC_INFO:
      xcb_discard_reply(dpy, randr_pass.crtc_info.sequence);
      break;
    case MONITOR_PASS_NONE:
      break;
    }
  }

  randr_pass.again = false;
  monitor_manager_pass_free_outputs();
  monitor_manager_pass_remove_poll();
}

/*
 * The loop's poll callback: read the answer the pass is waiting for, if it has
 * arrived, and take the next step.
 */
static void
monitor_manager_pass_poll(void* userdata)
{
  (void) userdata;

  if (dpy == NULL) {
    monitor_manager_pass_finish();
    return;
  }

  void*                raw_reply = NULL;
  xcb_generic_error_t* error     = NULL;

  switch (randr_pass.pass) {
  case MONITOR_PASS_RESOURCES: {
    /* Not the reply-forcing call: it reports "not yet" instead of waiting, so
       the loop stays free. */
    if (!xcb_poll_for_reply(dpy, randr_pass.resources.sequence, &raw_reply, &error))
      return;

    xcb_randr_get_screen_resources_reply_t* reply = raw_reply;
    if (reply != NULL && error == NULL) {
      int length = xcb_randr_get_screen_resources_outputs_length(reply);
      if (length > 0) {
        xcb_randr_output_t* listed = xcb_randr_get_screen_resources_outputs(reply);
        randr_pass.outputs         = malloc((size_t) length * sizeof *randr_pass.outputs);
        if (randr_pass.outputs != NULL) {
          memcpy(randr_pass.outputs, listed, (size_t) length * sizeof *randr_pass.outputs);
          randr_pass.output_count = length;
        } else {
          LOG_ERROR("Monitor manager: cannot hold the output list: %s", strerror(errno));
        }
      }
    } else if (error != NULL) {
      LOG_DEBUG("Monitor manager: the output list could not be read");
    }

    free(error);
    free(raw_reply);

    if (randr_pass.output_count == 0) {
      monitor_manager_pass_finish();
      return;
    }

    monitor_manager_pass_issue_output(0);
    return;
  }

  case MONITOR_PASS_OUTPUT_INFO: {
    if (!xcb_poll_for_reply(dpy, randr_pass.output_info.sequence, &raw_reply, &error))
      return;

    xcb_randr_crtc_t crtc = XCB_NONE;
    if (raw_reply != NULL && error == NULL)
      crtc = monitor_manager_apply_output_info(randr_pass.output, raw_reply);

    free(error);
    free(raw_reply);

    if (crtc == XCB_NONE) {
      monitor_manager_pass_next_output();
      return;
    }

    randr_pass.pass      = MONITOR_PASS_CRTC_INFO;
    randr_pass.crtc_info = xcb_randr_get_crtc_info(dpy, crtc, randr_pass.timestamp);
    return;
  }

  case MONITOR_PASS_CRTC_INFO: {
    if (!xcb_poll_for_reply(dpy, randr_pass.crtc_info.sequence, &raw_reply, &error))
      return;

    if (raw_reply != NULL && error == NULL)
      monitor_manager_apply_crtc_info(randr_pass.output, raw_reply);

    free(error);
    free(raw_reply);

    monitor_manager_pass_next_output();
    return;
  }

  case MONITOR_PASS_NONE:
    monitor_manager_pass_remove_poll();
    return;
  }
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
      /* The monitor exists as soon as the event says the output is there; the
         geometry comes with the pass, one round trip later. */
      if (monitor_get_by_output(oc->output) == NULL)
        monitor_manager_create_for_output(oc->output);
      (void) monitor_manager_pass_begin();
    }
    break;
  }

  case XCB_RANDR_NOTIFY_CRTC_CHANGE: {
    /* CRTC configuration changed - update affected output's monitor */
    xcb_randr_crtc_change_t* cc = &randr_event->u.cc;
    LOG_DEBUG("Monitor manager: CRTC change - crtc=%u, mode=%u, x=%d y=%d %ux%u",
              cc->crtc, cc->mode, cc->x, cc->y, cc->width, cc->height);

    monitor_manager_apply_crtc_geometry(cc->crtc, cc->x, cc->y, cc->width, cc->height);
    break;
  }

  case XCB_RANDR_NOTIFY_RESOURCE_CHANGE: {
    /* Resource change - refresh output list to detect changes */
    LOG_DEBUG("Monitor manager: Resource change notification");
    (void) monitor_manager_pass_begin();
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
 * Discover the monitors that are connected when the WM starts.
 *
 * This one waits, on purpose: the event loop does not exist yet, and a Monitor
 * has to be registered before the windows already on the display are adopted,
 * because that is when the other components attach their state machines to it.
 * Every later change goes through the pass above, which does not wait.
 */
static void
monitor_manager_discover_outputs_blocking(void)
{
  /* Check for valid X connection - skip discovery in test environment */
  if (dpy == NULL) {
    LOG_DEBUG("Monitor manager: No X connection, skipping output discovery");
    return;
  }

  /* Skip if root window is not set */
  if (root == XCB_NONE) {
    LOG_DEBUG("Monitor manager: Root window not set, skipping output discovery");
    return;
  }

  int                 output_count = 0;
  xcb_randr_output_t* outputs      = monitor_manager_read_screen_outputs_blocking(root, &output_count);

  if (outputs == NULL || output_count == 0) {
    LOG_DEBUG("Monitor manager: No RandR outputs found");
    return;
  }

  LOG_DEBUG("Monitor manager: Discovering %d RandR outputs", output_count);

  for (int i = 0; i < output_count; i++) {
    xcb_randr_output_t output = outputs[i];

    /* allow-blocking: startup, the event loop has not started yet */
    xcb_randr_get_output_info_reply_t* reply = xcb_randr_get_output_info_reply(
        dpy, xcb_randr_get_output_info(dpy, output, XCB_CURRENT_TIME), NULL);
    if (reply == NULL)
      continue;

    xcb_randr_crtc_t crtc = monitor_manager_apply_output_info(output, reply);
    free(reply);

    if (crtc == XCB_NONE)
      continue;

    /* allow-blocking: startup, the event loop has not started yet */
    xcb_randr_get_crtc_info_reply_t* crtc_reply = xcb_randr_get_crtc_info_reply(
        dpy, xcb_randr_get_crtc_info(dpy, crtc, XCB_CURRENT_TIME), NULL);
    if (crtc_reply == NULL)
      continue;

    monitor_manager_apply_crtc_info(output, crtc_reply);
    free(crtc_reply);
  }

  free(outputs);
}

/*
 * Get the outputs of the root window, waiting for the list.
 * Returns a newly allocated array (caller must free) or NULL on error.
 * Output count is stored in *count.
 *
 * Startup only: docs/PHILOSOPHY.md 2.3 has the loop read replies through its
 * poll callbacks instead.
 */
static xcb_randr_output_t*
monitor_manager_read_screen_outputs_blocking(xcb_window_t root, int* count)
{
  /* Check for valid X connection */
  if (dpy == NULL) {
    *count = 0;
    return NULL;
  }

  /* allow-blocking: startup, the event loop has not started yet */
  xcb_randr_get_screen_resources_reply_t* reply = xcb_randr_get_screen_resources_reply(
      dpy, xcb_randr_get_screen_resources(dpy, root), NULL);

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
 * Internal XCB handler dispatcher wrapper.
 * Called by xcb_handler_dispatch() for RandR events.
 */
static void
monitor_manager_xcb_handler(void* event)
{
  monitor_manager_handle_randr_notify(event);
}
