#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/* The display string this WM connected to. A child that opens its own X
   connection - the launcher's dmenu, the terminal, anything the WM spawns -
   has to reach the same server, and the environment it inherits is not proof
   that it will: the WM may have been started through a wrapper that set
   DISPLAY for one command and left it unset for the rest. */
static char* connected_display = NULL;

const char*
wm_xcb_display_name(void)
{
  return connected_display;
}

#ifdef WM_HUB_TESTING
void
wm_xcb_set_display_name_for_tests(const char* name)
{
  free(connected_display);
  connected_display = name == NULL ? NULL : strdup(name);
}
#endif

void
wm_xcb_close_connection_in_child(void)
{
  if (dpy == NULL)
    return;

  int fd = xcb_get_file_descriptor(dpy);
  if (fd >= 0)
    close(fd);
}

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

  free(connected_display);
  connected_display = strdup(displayname);
  if (connected_display == NULL)
    LOG_FATAL("wm: out of memory while recording the display name");

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

/*
 * Descriptors the loop waits on besides the X connection, and the callbacks it
 * runs when they are readable. A module that waits on a child process registers
 * its pipe here instead of reading the pipe in its own action callback, which
 * would stop the loop until the child answered.
 */
typedef struct WmLoopFdEntry {
  int             fd;
  WmLoopFdHandler handler;
  void*           userdata;
} WmLoopFdEntry;

static WmLoopFdEntry* fd_entries     = NULL;
static size_t         fd_entry_count = 0;
static size_t         fd_entry_cap   = 0;

bool
wm_loop_add_fd_handler(int fd, WmLoopFdHandler handler, void* userdata)
{
  if (fd < 0 || handler == NULL) {
    LOG_ERROR("cannot watch descriptor %d: no descriptor or no handler", fd);
    return false;
  }

  for (size_t i = 0; i < fd_entry_count; i++) {
    if (fd_entries[i].fd == fd) {
      LOG_ERROR("descriptor %d is already watched by the loop", fd);
      return false;
    }
  }

  if (fd_entry_count == fd_entry_cap) {
    size_t         cap   = fd_entry_cap == 0 ? 4 : fd_entry_cap * 2;
    WmLoopFdEntry* grown = realloc(fd_entries, cap * sizeof *grown);
    if (grown == NULL) {
      LOG_ERROR("cannot grow the loop's descriptor table: %s", strerror(errno));
      return false;
    }
    fd_entries   = grown;
    fd_entry_cap = cap;
  }

  fd_entries[fd_entry_count].fd       = fd;
  fd_entries[fd_entry_count].handler  = handler;
  fd_entries[fd_entry_count].userdata = userdata;
  fd_entry_count++;
  return true;
}

void
wm_loop_remove_fd_handler(int fd)
{
  for (size_t i = 0; i < fd_entry_count; i++) {
    if (fd_entries[i].fd != fd)
      continue;

    /* Order does not matter in this table: the loop looks an entry up by its
       descriptor, never by its position. */
    fd_entries[i] = fd_entries[fd_entry_count - 1];
    fd_entry_count--;
    return;
  }
}

int
wm_loop_dispatch_fd_handlers(void)
{
  size_t count = fd_entry_count;
  if (count == 0)
    return 0;

  /* Snapshot the descriptors before calling anything: a handler that is done
     with its descriptor removes it, and the table moves under this loop. */
  int* fds = malloc(count * sizeof *fds);
  if (fds == NULL) {
    LOG_ERROR("cannot dispatch the loop's descriptors: %s", strerror(errno));
    return 0;
  }
  for (size_t i = 0; i < count; i++)
    fds[i] = fd_entries[i].fd;

  int dispatched = 0;
  for (size_t i = 0; i < count; i++) {
    struct pollfd pfd = { .fd = fds[i], .events = POLLIN, .revents = 0 };
    if (poll(&pfd, 1, 0) <= 0)
      continue;
    /* POLLHUP is how a pipe whose writer has gone looks: the handler reads the
       end of file and removes itself. POLLNVAL is a descriptor that is already
       closed, and there is nothing to call for it. */
    if ((pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0)
      continue;

    /* Look the entry up again: an earlier handler may have removed it. */
    for (size_t j = 0; j < fd_entry_count; j++) {
      if (fd_entries[j].fd != fds[i])
        continue;
      fd_entries[j].handler(fd_entries[j].fd, fd_entries[j].userdata);
      dispatched++;
      break;
    }
  }

  free(fds);
  return dispatched;
}

/*
 * Callbacks the loop runs once per iteration, after the events are dispatched
 * and before the requests are flushed. A reply is not an event, so a module
 * that sent a cookie reads its answer here: xcb_poll_for_reply() says "not
 * yet" instead of waiting, and the loop is free to block in poll(2) until the
 * server answers.
 */
typedef struct WmLoopPollEntry {
  WmLoopPollHandler handler;
  void*             userdata;
} WmLoopPollEntry;

static WmLoopPollEntry* poll_entries     = NULL;
static size_t           poll_entry_count = 0;
static size_t           poll_entry_cap   = 0;

bool
wm_loop_add_poll_handler(WmLoopPollHandler handler, void* userdata)
{
  if (handler == NULL) {
    LOG_ERROR("cannot add a poll handler without a callback");
    return false;
  }

  for (size_t i = 0; i < poll_entry_count; i++) {
    if (poll_entries[i].handler == handler && poll_entries[i].userdata == userdata) {
      LOG_ERROR("this poll handler is already registered with the loop");
      return false;
    }
  }

  if (poll_entry_count == poll_entry_cap) {
    size_t           cap   = poll_entry_cap == 0 ? 4 : poll_entry_cap * 2;
    WmLoopPollEntry* grown = realloc(poll_entries, cap * sizeof *grown);
    if (grown == NULL) {
      LOG_ERROR("cannot grow the loop's poll table: %s", strerror(errno));
      return false;
    }
    poll_entries   = grown;
    poll_entry_cap = cap;
  }

  poll_entries[poll_entry_count].handler  = handler;
  poll_entries[poll_entry_count].userdata = userdata;
  poll_entry_count++;
  return true;
}

void
wm_loop_remove_poll_handler(WmLoopPollHandler handler, void* userdata)
{
  for (size_t i = 0; i < poll_entry_count; i++) {
    if (poll_entries[i].handler != handler || poll_entries[i].userdata != userdata)
      continue;

    /* Order means nothing here: the table is only ever walked whole. */
    poll_entries[i] = poll_entries[poll_entry_count - 1];
    poll_entry_count--;
    return;
  }
}

int
wm_loop_dispatch_poll_handlers(void)
{
  size_t count = poll_entry_count;
  if (count == 0)
    return 0;

  /* Snapshot the callbacks before calling anything: a handler may register or
     remove one, and the table moves under this loop. */
  WmLoopPollEntry* snapshot = malloc(count * sizeof *snapshot);
  if (snapshot == NULL) {
    LOG_ERROR("cannot dispatch the loop's poll handlers: %s", strerror(errno));
    return 0;
  }
  for (size_t i = 0; i < count; i++)
    snapshot[i] = poll_entries[i];

  int dispatched = 0;
  for (size_t i = 0; i < count; i++) {
    /* Look the entry up again: an earlier handler may have removed it. */
    bool still_registered = false;
    for (size_t j = 0; j < poll_entry_count; j++) {
      if (poll_entries[j].handler != snapshot[i].handler ||
          poll_entries[j].userdata != snapshot[i].userdata)
        continue;
      still_registered = true;
      break;
    }
    if (!still_registered)
      continue;

    snapshot[i].handler(snapshot[i].userdata);
    dispatched++;
  }

  free(snapshot);
  return dispatched;
}

void
destruct_xcb()
{
  /* Shutdown XCB handler registry */
  xcb_handler_shutdown();

  /* Tear down any remaining managed clients before disconnecting XCB */
  client_list_shutdown();

  free(queued_event);
  queued_event = NULL;

  free(fd_entries);
  fd_entries     = NULL;
  fd_entry_count = 0;
  fd_entry_cap   = 0;

  free(poll_entries);
  poll_entries     = NULL;
  poll_entry_count = 0;
  poll_entry_cap   = 0;

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
  int timeout_ms = -1;
  /* The X connection and the signal wakeup pipe keep the first two slots;
     everything registered with wm_loop_add_fd_handler() follows them. */
  size_t        watched = fd_entry_count;
  struct pollfd fds[2 + watched];

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

  for (size_t i = 0; i < watched; i++) {
    fds[2 + i].fd      = fd_entries[i].fd;
    fds[2 + i].events  = POLLIN;
    fds[2 + i].revents = 0;
  }

  if (fds[1].fd < 0) {
    /* No wakeup pipe: a signal cannot interrupt the wait, so a timeout is the
       only way SIGTERM is ever noticed. */
    timeout_ms = 250;
  }

  if (poll(fds, 2 + watched, timeout_ms) < 0 && errno != EINTR)
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

  /* The other descriptors the wait covered. A handler reads what is there and
     starts work; it never waits, which is why this is here and not in the
     action that registered it. */
  wm_loop_dispatch_fd_handlers();

  /* A reply is not an event either: a module that sent a cookie polls for its
     answer here. The poll reports "not yet" rather than waiting, so the loop
     stays free and blocks again on the next iteration. */
  wm_loop_dispatch_poll_handlers();

  /* Handlers queue requests and change the window tree; one flush per
   * iteration sends them without waiting on a reply (PHILOSOPHY 2.3). */
  xcb_flush(dpy);
}
