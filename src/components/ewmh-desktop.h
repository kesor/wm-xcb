/*
 * EWMH Desktop Component
 *
 * Publishes the tag model as EWMH desktops, so a panel or pager that knows
 * nothing about this WM can find it, list the windows it manages, name the
 * desktops, and switch between them.
 *
 * Root properties written at startup and kept current afterwards:
 * - _NET_SUPPORTING_WM_CHECK, on the root and on a window of its own, so a
 *   client can tell a live WM from a stale property
 * - _NET_SUPPORTED, the atoms below
 * - _NET_NUMBER_OF_DESKTOPS, one per tag
 * - _NET_DESKTOP_NAMES, the configured tag names, NUL-separated
 * - _NET_DESKTOP_VIEWPORT, all zero: a desktop here is a tag, not a scroll
 * - _NET_CURRENT_DESKTOP, the lowest visible tag of the selected monitor
 * - _NET_CLIENT_LIST, the managed windows in mapping order
 * - _NET_WM_DESKTOP on every managed window
 *
 * XCB events handled:
 * - XCB_CLIENT_MESSAGE with _NET_CURRENT_DESKTOP: a pager asked for a desktop,
 *   so the component sends REQ_TAG_VIEW for that tag to the monitor
 *
 * Events subscribed:
 * - EVT_TAG_CHANGED, which carries a monitor's new visible mask: the current
 *   desktop follows it, and every client's desktop is rewritten because the
 *   same event means "this client moved to another tag" when its target is a
 *   client
 * - EVT_CLIENT_MANAGED, EVT_CLIENT_UNMANAGED, EVT_CLIENT_DESTROYED: the client
 *   list changed
 * - EVT_CONFIG_RELOADED: the tag names were reread, so _NET_DESKTOP_NAMES is
 *   written again
 *
 * The component reads tag state only from events. It never asks the tag
 * manager, and it sends a request instead of setting a tag itself, so a panel
 * takes the same path a keybinding does.
 */

#ifndef _COMPONENT_EWMH_DESKTOP_H_
#define _COMPONENT_EWMH_DESKTOP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xcb/xcb.h>

#include "src/event-types.h"
#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"

/*
 * Component name
 */
#define EWMH_DESKTOP_COMPONENT_NAME "ewmh-desktop"

/*
 * _NET_WM_DESKTOP value for a client EWMH cannot place on one desktop, either
 * because it is on several tags or because it is on none.
 */
#define EWMH_DESKTOP_ALL            0xFFFFFFFFu

/*
 * EWMH Desktop component structure
 */
typedef struct EwmhDesktopComponent {
  /* Base component interface */
  HubComponent base;

  /* The window _NET_SUPPORTING_WM_CHECK points at, or XCB_NONE before setup
   * and after shutdown. */
  xcb_window_t check_window;

  /* Component-specific state */
  bool initialized;
} EwmhDesktopComponent;

/*
 * Check if component is initialized (for testing)
 */
bool ewmh_desktop_component_is_initialized(void);

/*
 * Initialize the component: register with the hub, subscribe to the events
 * above, register the ClientMessage handler, and write the root properties.
 *
 * Without an X connection - the unit suite - the model and the registration
 * still happen and only the property writes are skipped.
 *
 * Returns false when the hub refused the registration or the handler, so
 * main() stops rather than running with a WM no panel can find.
 */
bool ewmh_desktop_component_init(void);

/*
 * Shutdown the component and destroy the check window.
 */
void ewmh_desktop_component_shutdown(void);

/*
 * The desktop a client's tag mask names in _NET_WM_DESKTOP.
 *
 * EWMH gives a window exactly one desktop, with EWMH_DESKTOP_ALL for "on every
 * desktop". A client on exactly one tag reports that tag's index; a client on
 * several or on none reports EWMH_DESKTOP_ALL.
 */
uint32_t ewmh_desktop_for_client_tags(uint32_t tags);

/*
 * The desktop index a visible tag mask names in _NET_CURRENT_DESKTOP.
 *
 * EWMH names one desktop, and the tag view can show several, so the lowest
 * visible tag is the one reported. No visible tag reports desktop 0.
 */
uint32_t ewmh_desktop_for_visible_tags(uint32_t visible_tags);

/*
 * Write the NUL-separated desktop names EWMH expects into `buf`, at most
 * `size` bytes, and return the number of bytes the whole list needs.
 *
 * A name the config file did not supply is the tag number the user types.
 * Copying only whole names, and returning the needed size, is what lets a
 * caller size the buffer first and lets a short buffer stay a valid list.
 */
size_t ewmh_desktop_names(char* buf, size_t size);

/*
 * XCB handler for XCB_CLIENT_MESSAGE.
 * A message that is not _NET_CURRENT_DESKTOP with a valid desktop is ignored.
 */
void ewmh_desktop_on_client_message(void* event);

#endif /* _COMPONENT_EWMH_DESKTOP_H_ */
