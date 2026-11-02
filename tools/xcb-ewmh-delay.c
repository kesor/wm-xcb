/*
 * xcb-ewmh-delay - LD_PRELOAD shim that holds the first EWMH atom query.
 *
 * tests/x/test-86-queued-event.sh needs an event sitting in the WM's libxcb
 * queue while the X socket has nothing left to read. libxcb only fills that
 * queue during a reply wait or a flush, so the test needs the WM to be between
 * "an event has arrived" and "the WM reads the socket".
 *
 * setup_ewmh() waits for the server after substructure redirect is selected
 * and before wm_xcb_adopt_existing_windows(). Holding xcb_ewmh_init_atoms()
 * there is a point where a map becomes a MapRequest and the window stays
 * unmapped, so the adoption scan skips it. The reply wait reads that
 * MapRequest into libxcb's queue along with the replies. Later startup reply
 * waits do not put it back on the socket.
 *
 * The shim writes WM_QUEUE_TEST_MARKER once it is holding, and continues when
 * WM_QUEUE_TEST_GO exists. Both names are read from the environment; when
 * either is unset the wrapper is a plain pass-through. The wait is bounded so a
 * test that dies before writing the second file does not hang the WM.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

typedef xcb_intern_atom_cookie_t* (*init_atoms_fn)(xcb_connection_t*, xcb_ewmh_connection_t*);

static init_atoms_fn real_init_atoms = NULL;

static void
wait_for_release(void)
{
  const char* marker = getenv("WM_QUEUE_TEST_MARKER");
  const char* go     = getenv("WM_QUEUE_TEST_GO");

  if (marker == NULL || go == NULL)
    return;

  FILE* file = fopen(marker, "w");
  if (file != NULL) {
    fputs("ready\n", file);
    fclose(file);
  }

  /* 5 s in 10 ms steps. */
  for (int i = 0; i < 500; i++) {
    if (access(go, F_OK) == 0)
      return;
    usleep(10000);
  }
}

xcb_intern_atom_cookie_t*
xcb_ewmh_init_atoms(xcb_connection_t* conn, xcb_ewmh_connection_t* ewmh)
{
  static bool held = false;

  if (!held) {
    held = true;
    wait_for_release();
  }

  if (real_init_atoms == NULL)
    real_init_atoms = (init_atoms_fn) dlsym(RTLD_NEXT, "xcb_ewmh_init_atoms");

  return real_init_atoms(conn, ewmh);
}
