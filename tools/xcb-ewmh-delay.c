/*
 * xcb-ewmh-delay - LD_PRELOAD shim that holds the first EWMH atom query.
 *
 * tests/x/test-86-queued-event.sh needs an event sitting in the WM's libxcb
 * queue while the X socket has nothing left to read. libxcb only fills that
 * queue during a reply wait or a flush, so the test needs the WM to be between
 * "an event has arrived" and "the WM reads the socket".
 *
 * setup_ewmh() runs after the startup window rescan and before the event loop,
 * and it is the last startup step that waits for the server. Holding
 * xcb_ewmh_init_atoms() there gives the test a point after the rescan where it
 * can map a window. The real call sends the atom requests, and the reply wait
 * that follows reads the pending MapRequest into libxcb's queue along with the
 * replies.
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
