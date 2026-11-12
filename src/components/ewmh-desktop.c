/*
 * EWMH Desktop Component Implementation
 *
 * Writes the EWMH desktop properties a panel or pager reads, keeps them in
 * step with the tag manager's events, and turns a pager's desktop request into
 * the same tag request a keybinding sends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "src/components/client-list.h"
#include "src/components/ewmh-desktop.h"
#include "src/components/tag-manager.h"
#include "src/config/config.h"
#include "src/core/wm-hub.h"
#include "src/core/wm-log.h"
#include "src/core/wm-xcb-ewmh.h"
#include "src/core/wm-xcb.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"

/*
 * The name _NET_WM_NAME carries on the check window. A client that finds
 * _NET_SUPPORTING_WM_CHECK reads it to tell a user which WM answered.
 */
#define EWMH_WM_NAME        "wm-xcb"

/*
 * Longest tag number written for a tag the config file did not name.
 */
#define EWMH_TAG_NUMBER_MAX 8

static EwmhDesktopComponent ewmh_desktop_component = {
  .base = {
           .name                  = EWMH_DESKTOP_COMPONENT_NAME,
           .requests              = NULL,
           .accepted_target_names = NULL,
           .accepted_targets      = NULL,
           .executor              = NULL,
           .registered            = false,
           },
  .check_window = XCB_NONE,
  .initialized  = false,
};

/*
 * Whether the X half of the component can run. The unit suite has no server,
 * and the properties are then simply not there to write.
 */
static bool
ewmh_has_display(void)
{
  return dpy != NULL && ewmh != NULL && root != XCB_NONE;
}

/*
 * The name to publish for a tag. The config file names the tags it knows; the
 * rest keep the number a user types.
 */
static const char*
ewmh_tag_name(int index, char* number, size_t size)
{
  const char* name = config_get_tag_name(index);
  if (name != NULL && name[0] != '\0')
    return name;

  snprintf(number, size, "%d", index + 1);
  return number;
}

uint32_t
ewmh_desktop_for_client_tags(uint32_t tags)
{
  uint32_t desktop       = EWMH_DESKTOP_ALL;
  uint32_t set_tag_count = 0;

  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    if ((tags & TAG_MASK(i)) == 0)
      continue;
    desktop = (uint32_t) i;
    set_tag_count++;
  }

  return set_tag_count == 1 ? desktop : EWMH_DESKTOP_ALL;
}

uint32_t
ewmh_desktop_for_visible_tags(uint32_t visible_tags)
{
  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    if ((visible_tags & TAG_MASK(i)) != 0)
      return (uint32_t) i;
  }

  return 0;
}

size_t
ewmh_desktop_names(char* buf, size_t size)
{
  size_t needed = 0;

  for (int i = 0; i < TAG_NUM_TAGS; i++) {
    char        number[EWMH_TAG_NUMBER_MAX];
    const char* name = ewmh_tag_name(i, number, sizeof number);
    size_t      len  = strlen(name) + 1;

    if (buf != NULL && needed + len <= size)
      memcpy(buf + needed, name, len);
    needed += len;
  }

  return needed;
}

static void
ewmh_desktop_write_supported(void)
{
  xcb_atom_t supported[] = {
    ewmh->_NET_SUPPORTING_WM_CHECK,
    ewmh->_NET_CLIENT_LIST,
    ewmh->_NET_NUMBER_OF_DESKTOPS,
    ewmh->_NET_DESKTOP_NAMES,
    ewmh->_NET_DESKTOP_VIEWPORT,
    ewmh->_NET_CURRENT_DESKTOP,
    ewmh->_NET_WM_DESKTOP,
    ewmh->_NET_WM_STATE,
    ewmh->_NET_WM_STATE_FULLSCREEN,
  };

  xcb_ewmh_set_supported(
      ewmh,
      0,
      (uint32_t) (sizeof supported / sizeof supported[0]),
      supported);
}

static void
ewmh_desktop_write_names(void)
{
  size_t len  = ewmh_desktop_names(NULL, 0);
  char*  blob = malloc(len);
  if (blob == NULL) {
    LOG_ERROR("ewmh-desktop: cannot allocate desktop names");
    return;
  }

  ewmh_desktop_names(blob, len);
  xcb_change_property(dpy,
                      XCB_PROP_MODE_REPLACE,
                      root,
                      ewmh->_NET_DESKTOP_NAMES,
                      ewmh->UTF8_STRING,
                      8,
                      (uint32_t) len,
                      blob);
  free(blob);
}

static void
ewmh_desktop_write_viewports(void)
{
  xcb_ewmh_coordinates_t viewports[TAG_NUM_TAGS];

  memset(viewports, 0, sizeof viewports);
  xcb_ewmh_set_desktop_viewport(ewmh, 0, TAG_NUM_TAGS, viewports);
}

/*
 * The managed windows in mapping order.
 *
 * The client list holds the newest window first, and EWMH asks for the order
 * the windows were mapped in, so the collected ids are reversed before the
 * property is written. Unmanaged windows are not in the list at all: a panel
 * must not offer a window the WM is about to stop managing.
 *
 * `gone` is the window the event that triggered the write was about. The
 * client list is still holding a window that is being destroyed when that
 * event is emitted, so it is left out here rather than offered to a panel for
 * the moment between the notice and the removal. Window id 0 means none.
 */
static void
ewmh_desktop_write_client_list(xcb_window_t gone)
{
  uint32_t      max     = client_count_managed();
  xcb_window_t* windows = max > 0 ? calloc(max, sizeof *windows) : NULL;
  uint32_t      count   = 0;

  /* An allocation that failed leaves nothing to collect into, so the property
   * is written empty rather than from a NULL array. */
  if (windows == NULL)
    max = 0;

  for (Client* c = client_list_get_head(); c != NULL; c = client_get_next(c)) {
    if (!client_is_managed(c) || c->window == gone || count == max)
      continue;
    windows[count++] = c->window;
  }

  for (uint32_t i = 0; i < count / 2; i++) {
    xcb_window_t swap      = windows[i];
    windows[i]             = windows[count - 1 - i];
    windows[count - 1 - i] = swap;
  }

  xcb_ewmh_set_client_list(ewmh, 0, count, windows);
  free(windows);
}

static void
ewmh_desktop_write_current_desktop(uint32_t visible_tags)
{
  xcb_ewmh_set_current_desktop(ewmh, 0, ewmh_desktop_for_visible_tags(visible_tags));
}

static void
ewmh_desktop_write_client_desktops(void)
{
  for (Client* c = client_list_get_head(); c != NULL; c = client_get_next(c)) {
    if (!client_is_managed(c))
      continue;
    xcb_ewmh_set_wm_desktop(ewmh, c->window, ewmh_desktop_for_client_tags(c->tags));
  }
}

/*
 * The check window is InputOnly: EWMH needs a window id that lives as long as
 * the WM, and nothing is ever drawn there.
 */
static void
ewmh_desktop_create_check_window(void)
{
  ewmh_desktop_component.check_window = xcb_generate_id(dpy);
  /* An InputOnly window has no depth: the server rejects any depth but 0, so
   * XCB_COPY_FROM_PARENT (which is 0) means "no depth" here, not "the parent's". */
  xcb_create_window(dpy,
                    0,
                    ewmh_desktop_component.check_window,
                    root,
                    -1,
                    -1,
                    1,
                    1,
                    0,
                    XCB_WINDOW_CLASS_INPUT_ONLY,
                    XCB_COPY_FROM_PARENT,
                    0,
                    NULL);

  xcb_ewmh_set_supporting_wm_check(ewmh, root, ewmh_desktop_component.check_window);
  xcb_ewmh_set_supporting_wm_check(ewmh, ewmh_desktop_component.check_window,
                                   ewmh_desktop_component.check_window);
  xcb_ewmh_set_wm_name(ewmh, ewmh_desktop_component.check_window, strlen(EWMH_WM_NAME), EWMH_WM_NAME);
}

/*
 * Write the properties that only change with the configuration: the check
 * window and what points at it, the supported list, the desktop count and
 * names, and the zero viewports. Called once, at init.
 */
static void
ewmh_desktop_write_root_properties(uint32_t visible_tags)
{
  if (!ewmh_has_display())
    return;

  ewmh_desktop_create_check_window();
  ewmh_desktop_write_supported();
  xcb_ewmh_set_number_of_desktops(ewmh, 0, TAG_NUM_TAGS);
  ewmh_desktop_write_names();
  ewmh_desktop_write_viewports();
  ewmh_desktop_write_current_desktop(visible_tags);
  ewmh_desktop_write_client_list(XCB_NONE);
  ewmh_desktop_write_client_desktops();
}

/*
 * A pager asked for a desktop. The tag manager owns the view, so this sends the
 * request a keybinding sends rather than setting the tag itself.
 */
static void
ewmh_desktop_switch_to(const xcb_client_message_event_t* event)
{
  uint32_t desktop = event->data.data32[0];

  if (desktop >= TAG_NUM_TAGS) {
    LOG_DEBUG("ewmh-desktop: ignoring a request for desktop %u", desktop);
    return;
  }

  TargetID monitor = monitor_get_current_monitor();
  if (monitor == TARGET_ID_NONE) {
    LOG_DEBUG("ewmh-desktop: no monitor to switch the desktop on");
    return;
  }

  /* REQ_TAG_VIEW takes the tag number a user types, so desktop 0 is tag 1. */
  uint32_t tag = desktop + 1;
  hub_send_request_data(REQ_TAG_VIEW, monitor, &tag);
}

void
ewmh_desktop_on_client_message(void* event)
{
  xcb_client_message_event_t* message = (xcb_client_message_event_t*) event;

  if (message == NULL || ewmh == NULL || message->type != ewmh->_NET_CURRENT_DESKTOP)
    return;

  ewmh_desktop_switch_to(message);
}

/*
 * EVT_TAG_CHANGED means a monitor's visible tags changed - the event then
 * carries the new mask - or that a client moved to another tag, which carries
 * no mask. The client desktops are rewritten either way.
 */
static void
ewmh_desktop_on_tag_changed(Event e)
{
  if (!ewmh_has_display())
    return;

  if (e.data != NULL)
    ewmh_desktop_write_current_desktop(*(const uint32_t*) e.data);

  ewmh_desktop_write_client_desktops();
}

static void
ewmh_desktop_on_client_managed(Event e)
{
  if (!ewmh_has_display())
    return;

  Client* c = client_get_by_window((xcb_window_t) e.target);
  if (client_is_managed(c))
    xcb_ewmh_set_wm_desktop(ewmh, c->window, ewmh_desktop_for_client_tags(c->tags));

  ewmh_desktop_write_client_list(XCB_NONE);
}

static void
ewmh_desktop_on_client_gone(Event e)
{
  if (!ewmh_has_display())
    return;

  ewmh_desktop_write_client_list((xcb_window_t) e.target);
}

/*
 * A reload replaced the tag names. The desktop count does not change with the
 * configuration, so only the names property is written again.
 */
static void
ewmh_desktop_on_config_reloaded(Event e)
{
  (void) e;

  if (!ewmh_has_display())
    return;

  ewmh_desktop_write_names();
}

static void
ewmh_desktop_subscribe(void)
{
  hub_subscribe(EVT_TAG_CHANGED, ewmh_desktop_on_tag_changed, NULL);
  hub_subscribe(EVT_CLIENT_MANAGED, ewmh_desktop_on_client_managed, NULL);
  hub_subscribe(EVT_CLIENT_UNMANAGED, ewmh_desktop_on_client_gone, NULL);
  hub_subscribe(EVT_CLIENT_DESTROYED, ewmh_desktop_on_client_gone, NULL);
  hub_subscribe(EVT_CONFIG_RELOADED, ewmh_desktop_on_config_reloaded, NULL);
}

static void
ewmh_desktop_unsubscribe(void)
{
  hub_unsubscribe(EVT_TAG_CHANGED, ewmh_desktop_on_tag_changed);
  hub_unsubscribe(EVT_CLIENT_MANAGED, ewmh_desktop_on_client_managed);
  hub_unsubscribe(EVT_CLIENT_UNMANAGED, ewmh_desktop_on_client_gone);
  hub_unsubscribe(EVT_CLIENT_DESTROYED, ewmh_desktop_on_client_gone);
  hub_unsubscribe(EVT_CONFIG_RELOADED, ewmh_desktop_on_config_reloaded);
}

static void
ewmh_desktop_reset(void)
{
  ewmh_desktop_component.check_window = XCB_NONE;
  ewmh_desktop_component.initialized  = false;
}

bool
ewmh_desktop_component_init(void)
{
  HubComponent* existing = hub_get_component_by_name(EWMH_DESKTOP_COMPONENT_NAME);
  if (existing != NULL) {
    if (ewmh_desktop_component.initialized) {
      LOG_DEBUG("ewmh-desktop: already initialized");
      return true;
    }
    LOG_DEBUG("ewmh-desktop: registered with the hub, completing initialization");
    ewmh_desktop_component.initialized = true;
    return true;
  }

  if (ewmh_desktop_component.initialized) {
    LOG_DEBUG("ewmh-desktop: inconsistent state, resetting");
    ewmh_desktop_reset();
  }

  if (hub_register_component(&ewmh_desktop_component.base) != 0) {
    ewmh_desktop_reset();
    return false;
  }

  if (xcb_handler_register(
          XCB_CLIENT_MESSAGE,
          &ewmh_desktop_component.base,
          ewmh_desktop_on_client_message) != 0) {
    LOG_ERROR("ewmh-desktop: cannot register the ClientMessage handler");
    hub_unregister_component(EWMH_DESKTOP_COMPONENT_NAME);
    ewmh_desktop_reset();
    return false;
  }

  ewmh_desktop_subscribe();

  /* Monitors are discovered after this runs, and adopting one makes the tag
   * manager emit EVT_TAG_CHANGED with its view. This first write only fills
   * the property, so it is there for a panel that starts before the first
   * monitor is known; the event is what makes it say something true. */
  ewmh_desktop_write_root_properties(TAG_MASK(0));

  ewmh_desktop_component.initialized = true;
  LOG_DEBUG("ewmh-desktop: initialized");
  return true;
}

void
ewmh_desktop_component_shutdown(void)
{
  if (!ewmh_desktop_component.initialized)
    return;

  ewmh_desktop_unsubscribe();
  xcb_handler_unregister_component(&ewmh_desktop_component.base);

  /* A client that reads _NET_SUPPORTING_WM_CHECK and then finds the window gone
   * knows the WM has left: the property is only a pointer to a window, and
   * closing the connection destroys it whichever request gets out first. */
  if (ewmh_has_display() && ewmh_desktop_component.check_window != XCB_NONE)
    xcb_destroy_window(dpy, ewmh_desktop_component.check_window);

  hub_unregister_component(EWMH_DESKTOP_COMPONENT_NAME);
  ewmh_desktop_reset();
  LOG_DEBUG("ewmh-desktop: shut down");
}

bool
ewmh_desktop_component_is_initialized(void)
{
  return ewmh_desktop_component.initialized;
}
