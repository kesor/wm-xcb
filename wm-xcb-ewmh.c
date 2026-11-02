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

void
print_atom_name(xcb_atom_t atom)
{
  xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(dpy, atom);
  xcb_get_atom_name_reply_t* reply  = xcb_get_atom_name_reply(dpy, cookie, NULL);
  if (reply == NULL) {
    LOG_ERROR("Failed to get atom name\n");
    return;
  }

  LOG_DEBUG("atom name: %s", xcb_get_atom_name_name(reply));

  free(reply);
}

/*
 * Check if _NET_WM_STATE contains _NET_WM_STATE_FULLSCREEN for a window.
 *
 * Uses xcb_get_property directly to get the raw atom list.
 */
bool
ewmh_check_wm_state_fullscreen(xcb_ewmh_connection_t* ewmh, xcb_window_t window)
{
  if (ewmh == NULL)
    return false;

  /* Get the _NET_WM_STATE property directly */
  xcb_get_property_cookie_t cookie = xcb_get_property_unchecked(
      dpy, 0, window, ewmh->_NET_WM_STATE, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024);

  xcb_get_property_reply_t* reply = xcb_get_property_reply(dpy, cookie, NULL);
  if (reply == NULL)
    return false;

  /* Check format (should be 32 bits for atoms) */
  if (reply->format != 32) {
    free(reply);
    return false;
  }

  /* Get length in atoms (format * length / 32 = number of atoms) */
  uint32_t length = xcb_get_property_value_length(reply);
  if (length == 0) {
    free(reply);
    return false;
  }

  xcb_atom_t* atoms = (xcb_atom_t*) xcb_get_property_value(reply);
  if (atoms == NULL) {
    free(reply);
    return false;
  }

  /* Check if _NET_WM_STATE_FULLSCREEN is in the list */
  uint32_t count = length / sizeof(xcb_atom_t);
  for (uint32_t i = 0; i < count; i++) {
    if (atoms[i] == ewmh->_NET_WM_STATE_FULLSCREEN) {
      free(reply);
      return true;
    }
  }

  free(reply);
  return false;
}
