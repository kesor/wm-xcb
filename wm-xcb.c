#include <xcb/errors.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include "src/target/client.h"
#include "src/xcb/xcb-handler.h"
#include "wm-log.h"
#include "wm-running.h"
#include "wm-states.h"
#include "wm-xcb-events.h"
#include "wm-xcb.h"

xcb_connection_t* dpy;
xcb_window_t      root;

void wm_manage_all_clients();

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
  char* displayname    = NULL;
  char* hostname       = NULL;
  int   display_number = 0;
  int   screen_number  = 0;

  if (xcb_parse_display(displayname, &hostname, &display_number, &screen_number) == 0)
    LOG_FATAL("dwm: failed to parse display name");

  /* connecting to remote hosts not really supported here with a simple xcb_connect() */
  free(hostname);

  *dpy = xcb_connect(displayname, &screen_number);
  if (xcb_connection_has_error(*dpy))
    LOG_FATAL("dwm: failed to open display");
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
  xcb_input_xi_query_version_reply_t* xi_reply  = xcb_input_xi_query_version_reply(dpy, xi_cookie, NULL);
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
  mask.mask          = 0 | XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS | XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE | XCB_INPUT_XI_EVENT_MASK_DEVICE_CHANGED | XCB_INPUT_XI_EVENT_MASK_HIERARCHY | XCB_INPUT_XI_EVENT_MASK_KEY_PRESS | XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE | XCB_INPUT_XI_EVENT_MASK_PROPERTY | XCB_INPUT_MOTION | XCB_INPUT_ENTER | XCB_INPUT_LEAVE | XCB_INPUT_FOCUS_IN | XCB_INPUT_FOCUS_OUT | XCB_INPUT_RAW_KEY_PRESS | XCB_INPUT_RAW_KEY_RELEASE | XCB_INPUT_RAW_BUTTON_PRESS | XCB_INPUT_RAW_BUTTON_RELEASE | XCB_INPUT_RAW_MOTION | XCB_INPUT_TOUCH_BEGIN | XCB_INPUT_TOUCH_UPDATE | XCB_INPUT_TOUCH_END | XCB_INPUT_TOUCH_OWNERSHIP | XCB_INPUT_RAW_TOUCH_BEGIN | XCB_INPUT_RAW_TOUCH_UPDATE | XCB_INPUT_RAW_TOUCH_END;

  xcb_input_xi_select_events(dpy, root, 1, &mask.head);
}

void
check_no_running_wm()
{
  xcb_void_cookie_t    wm_cookie = xcb_change_window_attributes_checked(dpy, root, XCB_CW_EVENT_MASK, (uint32_t[]) { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT });
  xcb_generic_error_t* err       = xcb_request_check(dpy, wm_cookie);
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

  wm_manage_all_clients();

  if (xcb_flush(dpy) <= 0)
    LOG_FATAL("failed to flush.");

  LOG_DEBUG("root window id: %d", root);
}

void
destruct_xcb()
{
  /* Shutdown XCB handler registry */
  xcb_handler_shutdown();

  /* Client list is cleaned up when clients are destroyed individually */
  xcb_disconnect(dpy);
}

void
wm_manage_all_clients()
{
  static uint32_t values[3];

  // events we want to "hijack" from child windows
  values[0] = 0 | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;

  // 1. query all the window descendants of the root window
  xcb_query_tree_cookie_t tree_cookie = xcb_query_tree_unchecked(dpy, root);
  xcb_query_tree_reply_t* reply       = xcb_query_tree_reply(dpy, tree_cookie, NULL);

  if (!reply)
    return;

  // ALL the windows, includeing children of children.
  xcb_window_t* children        = xcb_query_tree_children(reply);
  uint16_t      children_length = xcb_query_tree_children_length(reply);
  free(reply);

  // 2. create a data structure to store the cookies for each xcb_query_tree call of child windows
  xcb_query_tree_cookie_t* cookies = malloc(children_length * sizeof(xcb_query_tree_cookie_t));
  if (!cookies) {
    LOG_FATAL("could not allocate memory to store children cookies.");
    return;
  }

  // 3. query each child window for its own descendants & store the cookie
  for (uint16_t i = 0; i < children_length; i++) {
    xcb_window_t child = children[i];
    cookies[i]         = xcb_query_tree_unchecked(dpy, child);
  }

  // 4. check the reply for each such cookie to find the parent of this window
  for (uint16_t i = 0; i < children_length; i++) {
    xcb_window_t child = children[i];

    // 5. get the reply for this child window
    xcb_query_tree_reply_t* reply = xcb_query_tree_reply(dpy, cookies[i], NULL);
    if (!reply)
      continue;

    // 6. check if the child window is a direct descendant of the root window
    if (reply->parent == root) {
      xcb_reparent_window(dpy, child, root, 0, 0);
      xcb_change_window_attributes(dpy, child, XCB_CW_EVENT_MASK, values);
      xcb_map_window(dpy, child);
    }

    // 7. free the reply for this child window
    free(reply);
  }

  // 8. free the data structure that stores the cookies
  free(cookies);

  xcb_flush(dpy);
}

void
handle_xcb_events()
{
  xcb_generic_event_t* event = xcb_poll_for_event(dpy);
  if (event == NULL)
    return;

  /* Handle errors (response_type = 0) */
  if (event->response_type == 0) {
    error_details((xcb_generic_error_t*) event);
    free(event);
    return;
  }

  /* Dispatch to registered handlers */
  xcb_handler_dispatch(event);

  /* Handle state events (e.g., running flag changes) */
  handle_state_event(event);

  free(event);
}
