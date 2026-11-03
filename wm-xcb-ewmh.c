#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>

#include "wm-log.h"
#include "wm-xcb-ewmh.h"
#include "wm-xcb.h"

xcb_ewmh_connection_t* ewmh;

void
setup_ewmh()
{
  xcb_generic_error_t* error;
  ewmh = malloc(sizeof(xcb_ewmh_connection_t));
  memset(ewmh, 0, sizeof(xcb_ewmh_connection_t));
  xcb_intern_atom_cookie_t* cookie = xcb_ewmh_init_atoms(dpy, ewmh);
  if (!xcb_ewmh_init_atoms_replies(ewmh, cookie, &error)) {
    /* replies frees the per-screen lists on failure and leaves the pointers
       dangling. destruct_ewmh wipes again; a second free is a double-free. */
    ewmh->screens       = NULL;
    ewmh->_NET_WM_CM_Sn = NULL;
    LOG_FATAL("Failed to initialize EWMH atoms");
  }
}

void
destruct_ewmh()
{
  /* init_atoms allocates the screen list and one _NET_WM_CM_Sn atom per
     screen inside the struct; free() alone loses them. wipe dereferences
     its argument, unlike free, so a second call must not reach it. */
  if (ewmh == NULL)
    return;
  xcb_ewmh_connection_wipe(ewmh);
  free(ewmh);
  ewmh = NULL;
}

/*
 * Ask for the window's _NET_WM_STATE list.
 *
 * The caller keeps the cookie and reads the reply from the loop's poll
 * callback: `_NET_WM_STATE` arrives as a PropertyNotify, which says the list
 * changed but not what it now holds, and reading the reply here would stop the
 * WM until the server answered.
 */
xcb_get_property_cookie_t
ewmh_request_wm_state(xcb_ewmh_connection_t* ewmh, xcb_window_t window)
{
  return xcb_get_property_unchecked(
      dpy, 0, window, ewmh->_NET_WM_STATE, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024);
}

/*
 * Check whether a _NET_WM_STATE reply lists _NET_WM_STATE_FULLSCREEN.
 *
 * The reply stays the caller's: it is freed after this returns.
 */
bool
ewmh_wm_state_is_fullscreen(xcb_ewmh_connection_t* ewmh, xcb_get_property_reply_t* reply)
{
  if (ewmh == NULL || reply == NULL)
    return false;

  /* The list is a CARD32 list of atoms; any other format is not one. */
  if (reply->format != 32)
    return false;

  uint32_t length = xcb_get_property_value_length(reply);
  if (length < sizeof(xcb_atom_t))
    return false;

  xcb_atom_t* atoms = (xcb_atom_t*) xcb_get_property_value(reply);
  if (atoms == NULL)
    return false;

  uint32_t count = length / sizeof(xcb_atom_t);
  for (uint32_t i = 0; i < count; i++) {
    if (atoms[i] == ewmh->_NET_WM_STATE_FULLSCREEN)
      return true;
  }

  return false;
}
