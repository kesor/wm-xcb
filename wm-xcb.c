#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/errors.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include "src/components/client-list.h"
#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-log.h"
#include "wm-running.h"
#include "wm-signals.h"
#include "wm-states.h"
#include "wm-xcb-events.h"
#include "wm-xcb.h"

xcb_connection_t* dpy;
xcb_window_t      root;

void
error_details(xcb_generic_error_t* error)
{
  xcb_errors_context_t* err_ctx;
  xcb_errors_context_new(dpy, &err_ctx);
  const char *major, *minor, *extension, *error_name;
  major      = xcb_errors_get_name_for_major_code(err_ctx, error->major_code);
  minor      = xcb_errors_get_name_for_minor_code(err_ctx, error->major_code, error->minor_code);
  error_name = xcb_errors_get_name_for_error(err_ctx, error->error_code, &extension);
  LOG_ERROR("XCB: %s:%s, %s:%s, resource %u sequence %u",
            error_name, extension ? extension : "no_extension",
            major, minor ? minor : "no_minor",
            (unsigned int) error->resource_id,
            (unsigned int) error->sequence);
  xcb_errors_context_free(err_ctx);
}

static void
connect_to_x_display(xcb_connection_t** dpy)
{
  char* displayname    = getenv("DISPLAY");
  char* hostname       = NULL;
  int   display_number = 0;
  int   screen_number  = 0;

  if (displayname == NULL)
    LOG_FATAL("wm: DISPLAY environment variable not set");

  if (xcb_parse_display(displayname, &hostname, &display_number, &screen_number) == 0)
    LOG_FATAL("wm: failed to parse display name: %s", displayname);

  /* connecting to remote hosts not really supported here with a simple xcb_connect() */
  free(hostname);

  *dpy = xcb_connect(displayname, &screen_number);
  if (xcb_connection_has_error(*dpy))
    LOG_FATAL("wm: failed to open display");
}

void
get_root_window(xcb_connection_t* dpy, xcb_window_t* root)
{
  const xcb_setup_t* setup  = xcb_get_setup(dpy);
  xcb_screen_t*      screen = xcb_setup_roots_iterator(setup).data;
  *root                     = screen->root;
}

void
setup_xinput_initialize()
{
  xcb_input_xi_query_version_cookie_t xi_cookie = xcb_input_xi_query_version_unchecked(dpy, XCB_INPUT_MAJOR_VERSION, XCB_INPUT_MINOR_VERSION);
  /* allow-blocking: startup, the event loop has not started yet */
  xcb_input_xi_query_version_reply_t* xi_reply = xcb_input_xi_query_version_reply(dpy, xi_cookie, NULL);
  LOG_DEBUG("XInput version: %d.%d", xi_reply->major_version, xi_reply->minor_version);
  free(xi_reply);
}

void
setup_xinput_events()
{
  struct {
    xcb_input_event_mask_t    head;
    xcb_input_xi_event_mask_t mask;
  } mask;
  mask.head.deviceid = XCB_INPUT_DEVICE_ALL;
  mask.head.mask_len = sizeof(mask.mask) / sizeof(uint32_t);
  mask.mask          = (xcb_input_xi_event_mask_t) (0U | XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS | XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE | XCB_INPUT_XI_EVENT_MASK_DEVICE_CHANGED | XCB_INPUT_XI_EVENT_MASK_HIERARCHY | XCB_INPUT_XI_EVENT_MASK_KEY_PRESS | XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE | XCB_INPUT_XI_EVENT_MASK_PROPERTY | XCB_INPUT_MOTION | XCB_INPUT_ENTER | XCB_INPUT_LEAVE | XCB_INPUT_FOCUS_IN | XCB_INPUT_FOCUS_OUT | XCB_INPUT_RAW_KEY_PRESS | XCB_INPUT_RAW_KEY_RELEASE | XCB_INPUT_RAW_BUTTON_PRESS | XCB_INPUT_RAW_BUTTON_RELEASE | XCB_INPUT_RAW_MOTION | XCB_INPUT_TOUCH_BEGIN | XCB_INPUT_TOUCH_UPDATE | XCB_INPUT_TOUCH_END | XCB_INPUT_TOUCH_OWNERSHIP | XCB_INPUT_RAW_TOUCH_BEGIN | XCB_INPUT_RAW_TOUCH_UPDATE | XCB_INPUT_RAW_TOUCH_END);    // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

  xcb_input_xi_select_events(dpy, root, 1, &mask.head);
}

void
check_no_running_wm()
{
  xcb_void_cookie_t wm_cookie = xcb_change_window_attributes_checked(dpy, root, XCB_CW_EVENT_MASK, (uint32_t[]) { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT });
  /* allow-blocking: startup, deciding whether another WM owns the display */
  xcb_generic_error_t* err = xcb_request_check(dpy, wm_cookie);
  if (err) {
    error_details(err);
    free(err);
    LOG_FATAL("Another window manager is already running");
  }
}

void
setup_xcb()
{
  static uint32_t values[3];

  /* Initialize XCB handler registry before setting up events */
  xcb_handler_init();

  connect_to_x_display(&dpy);
  get_root_window(dpy, &root);

  client_atoms_init();
  client_list_init();

  check_no_running_wm();
  if (!running)
    return;

  setup_xinput_initialize();
  setup_xinput_events();

  values[0] = 0x00ffffff;    // "any" event (first 24 bits flipped on)

  const static uint32_t mask = XCB_CW_EVENT_MASK;
  xcb_change_window_attributes(dpy, root, mask, values);

  xcb_map_window(dpy, root);

  /* Note: root window is not a client */

  if (xcb_flush(dpy) <= 0)
    LOG_FATAL("failed to flush.");

  LOG_DEBUG("root window id: %d", root);
}

/* An event libxcb had already queued when the wait started. poll(2) reports
   the socket, not that queue, so it would block with the event still queued. */
static xcb_generic_event_t* queued_event = NULL;

void
destruct_xcb()
{
  /* Shutdown XCB handler registry */
  xcb_handler_shutdown();

  /* Tear down any remaining managed clients before disconnecting XCB */
  client_list_shutdown();

  free(queued_event);
  queued_event = NULL;

  xcb_disconnect(dpy);
}

/*
 * Hand a window that already exists to the components as if the server had
 * sent a MapRequest for it. A window that was created and mapped before the WM
 * started never produces one, and the MapRequest handler is the only code that
 * takes a window into the client model: it creates the Client target, maps the
 * window with a border and WM_STATE, and asks for the monitor to be tiled.
 * Repeating that work here would give the WM two manage paths that can drift,
 * and calling the client-list component directly would make this file know
 * which component manages a window.
 */
static void
adopt_window(xcb_window_t window)
{
  xcb_map_request_event_t event = {
    .response_type = XCB_MAP_REQUEST,
    .sequence      = 0,
    .parent        = root,
    .window        = window,
  };
  xcb_handler_dispatch(&event);
}

/*
 * Take over the windows that were on the display before the WM started.
 *
 * Runs after the components are initialized, not with the rest of the XCB
 * setup: creating a Client registers a target, which is when the components
 * attach their state machines to it, and the client is attached to a monitor,
 * which monitor discovery has to have created. Adopting earlier leaves the
 * clients without state machines and maps windows that nothing tiles.
 */
void
wm_xcb_adopt_existing_windows(void)
{
  xcb_query_tree_cookie_t tree_cookie = xcb_query_tree_unchecked(dpy, root);
  /* allow-blocking: startup, before the loop runs */
  xcb_query_tree_reply_t* tree = xcb_query_tree_reply(dpy, tree_cookie, NULL);
  if (tree == NULL) {
    LOG_WARN("could not read the window tree at startup: no window is adopted");
    return;
  }

  /* The children array is part of the reply, so the reply stays alive for as
     long as the array is read. */
  uint16_t      children_length = xcb_query_tree_children_length(tree);
  xcb_window_t* children        = xcb_query_tree_children(tree);
  if (children_length == 0) {
    free(tree);
    return;
  }

  xcb_get_window_attributes_cookie_t* cookies =
      calloc(children_length, sizeof(xcb_get_window_attributes_cookie_t));
  if (cookies == NULL) {
    LOG_ERROR("could not allocate memory for the startup window attributes.");
    free(tree);
    return;
  }

  for (uint16_t i = 0; i < children_length; i++)
    cookies[i] = xcb_get_window_attributes_unchecked(dpy, children[i]);

  for (uint16_t i = 0; i < children_length; i++) {
    xcb_window_t child = children[i];

    /* allow-blocking: startup rescan, before the loop runs */
    xcb_get_window_attributes_reply_t* attr =
        xcb_get_window_attributes_reply(dpy, cookies[i], NULL);
    if (attr == NULL)
      continue;

    bool override_redirect = attr->override_redirect;
    bool viewable          = attr->map_state == XCB_MAP_STATE_VIEWABLE;
    free(attr);

    /* An override-redirect window is a menu, a tooltip or similar: the client
       places it and the WM must keep its hands off it. A window that is not
       viewable was deliberately left off screen by its client, and mapping it
       would show a window nobody asked to see. */
    if (override_redirect || !viewable)
      continue;

    if (client_get_by_window(child) != NULL) {
      LOG_DEBUG("window %u is already a client, not adopting it again", child);
      continue;
    }

    LOG_DEBUG("adopting window %u that existed before the WM started", child);
    adopt_window(child);
  }

  free(cookies);
  free(tree);

  /* Adoption queues the border, property, mapping and tiling requests. The
     event loop only flushes after poll(2) reports an event, and poll(2) does
     not report a pending output buffer, so without this the windows would keep
     their old geometry until the first X event arrived. */
  if (xcb_flush(dpy) <= 0)
    LOG_ERROR("failed to flush the requests that adopt the existing windows");
}

void
wm_xcb_wait_for_events()
{
  struct pollfd fds[2];
  int           timeout_ms = -1;

  /* poll(2) on a connection that has already failed returns at once for ever,
     which is the busy loop again: there is nothing left to wait for. */
  if (xcb_connection_has_error(dpy) != 0) {
    LOG_ERROR("the X connection is gone: %d", xcb_connection_has_error(dpy));
    running = 0;
    return;
  }

  /* libxcb reads ahead: a reply wait during startup, or the flush at the end of
     the last dispatch, can move an event into its queue while the socket stays
     unreadable. Take that event now; handle_xcb_events() dispatches it before
     the drain, and the next wait picks up anything a flush read. */
  if (queued_event != NULL) {
    /* A second wait without a handle would leak the event already held. */
    free(queued_event);
    queued_event = NULL;
  }
  queued_event = xcb_poll_for_queued_event(dpy);
  if (queued_event != NULL)
    return;

  fds[0].fd      = xcb_get_file_descriptor(dpy);
  fds[0].events  = POLLIN;
  fds[0].revents = 0;
  fds[1].fd      = signals_wakeup_fd();
  fds[1].events  = POLLIN;
  fds[1].revents = 0;

  if (fds[1].fd < 0) {
    /* No wakeup pipe: a signal cannot interrupt the wait, so a timeout is the
       only way SIGTERM is ever noticed. */
    timeout_ms = 250;
  }

  if (poll(fds, 2, timeout_ms) < 0 && errno != EINTR)
    LOG_ERROR("poll on the X connection failed: %s", strerror(errno));

  signals_drain_wakeups();
}

static void
dispatch_event(xcb_generic_event_t* event)
{
  /* Handle errors (response_type = 0) */
  if (event->response_type == 0) {
    error_details((xcb_generic_error_t*) event);
    return;
  }

  /* Dispatch to registered handlers */
  xcb_handler_dispatch(event);

  /* Handle state events (e.g., running flag changes) */
  handle_state_event(event);
}

void
handle_xcb_events()
{
  xcb_generic_event_t* event;

  if (queued_event != NULL) {
    event        = queued_event;
    queued_event = NULL;
    dispatch_event(event);
    free(event);
  }

  /* Drain everything X has already given us. poll(2) says the socket is
     readable, not that one event arrived: a later poll on a socket whose
     events are all in libxcb's own queue waits for the next one. */
  while ((event = xcb_poll_for_event(dpy)) != NULL) {
    dispatch_event(event);
    free(event);
  }

  /* A property reply is not an event: libxcb keeps it in its queue until
   * somebody asks for it. Client size hints arrive that way, and a window
   * laid out before they were known has to be laid out again. */
  client_list_poll_size_hints();

  /* Handlers queue requests and change the window tree; one flush per
   * iteration sends them without waiting on a reply (PHILOSOPHY 2.3). */
  xcb_flush(dpy);
}
