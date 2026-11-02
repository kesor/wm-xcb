/*
 * xclient - minimal X11 test client for exercising the window manager.
 *
 * Creates one or more top-level windows with WM_NAME/WM_CLASS set, maps
 * them, and stays alive until killed or until -l seconds elapse.
 *
 * It exists because xterm and most graphical clients need fonts or image
 * loaders that a bare Xephyr session does not provide. This client needs
 * nothing beyond libxcb, so tiling, focus, tag, and performance tests can
 * run anywhere the WM builds.
 *
 * Build:  make tools/xclient
 * Usage:  xclient [-n COUNT] [-t TITLE] [-W WIDTH] [-H HEIGHT] [-s SLEEP]
 *                 [-l LIFETIME] [-c CLASS] [-o] [-h]
 *
 * -n COUNT     number of windows to create (default 1)
 * -t TITLE     window title prefix; each window is TITLE-<index>
 * -c CLASS     WM_CLASS/res_name and res_class (default "xclient")
 * -W/-H        requested width/height of each window (default 400x300)
 * -s SLEEP     sleep seconds between creating windows (default 0)
 * -y           wait for the server to process each map before printing its id
 * -l LIFETIME  exit after this many seconds (default: run until killed)
 * -o           create override-redirect windows, which a WM must leave alone
 * -h           print this help
 *
 * Each created window's XID is printed to stdout when -v is given, so a
 * test script can pass the ID to xdotool/xprop. Without -v the tool prints
 * a single line with the window count.
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <xcb/xcb.h>

static xcb_connection_t* conn;
static xcb_screen_t*     screen;
static xcb_atom_t        atom_wm_name;
static xcb_atom_t        atom_net_wm_name;
static xcb_atom_t        atom_utf8_string;

static xcb_atom_t
intern_atom(const char* name)
{
  xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
  xcb_intern_atom_reply_t* reply  = xcb_intern_atom_reply(conn, cookie, NULL);
  if (reply == NULL)
    return XCB_ATOM_NONE;
  xcb_atom_t atom = reply->atom;
  free(reply);
  return atom;
}

static void
set_prop32(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type, const uint32_t* values, uint32_t count)
{
  xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, prop, type, 32, count, values);
}

static void
set_prop8(xcb_window_t win, xcb_atom_t prop, xcb_atom_t type, const char* value)
{
  xcb_change_property(
      conn, XCB_PROP_MODE_REPLACE, win, prop, type, 8, (uint32_t) strlen(value), value);
}

/*
 * Sets both ICCCM WM_NAME and EWMH _NET_WM_NAME so the WM can read a title
 * whichever it prefers.
 */
static void
set_title(xcb_window_t win, const char* title)
{
  set_prop8(win, atom_wm_name, XCB_ATOM_STRING, title);
  set_prop8(win, atom_net_wm_name, atom_utf8_string, title);
}

/*
 * WM_CLASS holds two consecutive NUL-terminated strings: res_name then
 * res_class.
 */
static void
set_class(xcb_window_t win, const char* res_name, const char* res_class)
{
  size_t len = strlen(res_name) + 1 + strlen(res_class) + 1;
  char*  buf = calloc(1, len);
  if (buf == NULL)
    return;
  strcpy(buf, res_name);
  strcpy(buf + strlen(res_name) + 1, res_class);
  xcb_change_property(
      conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, (uint32_t) len, buf);
  free(buf);
}

static xcb_window_t
create_window(
    const char* title, const char* klass, uint32_t width, uint32_t height, bool override_redirect)
{
  xcb_window_t win = xcb_generate_id(conn);

  /* The value list follows the order of the mask bits, and
     XCB_CW_OVERRIDE_REDIRECT sits between XCB_CW_BACK_PIXEL and
     XCB_CW_EVENT_MASK. */
  uint32_t mask = XCB_CW_BACK_PIXEL;
  uint32_t values[3];
  uint32_t next  = 0;
  values[next++] = screen->black_pixel;
  if (override_redirect) {
    mask |= XCB_CW_OVERRIDE_REDIRECT;
    values[next++] = 1;
  }
  mask |= XCB_CW_EVENT_MASK;
  values[next++] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                   XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE;

  xcb_create_window(conn,
                    XCB_COPY_FROM_PARENT,
                    win,
                    screen->root,
                    0,
                    0,
                    width,
                    height,
                    1,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    mask,
                    values);

  set_title(win, title);
  set_class(win, klass, klass);

  /* Normal gravity so the WM decides placement. */
  uint32_t gravity = XCB_GRAVITY_NORTH_WEST;
  set_prop32(win, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, &gravity, 1);

  /* Report a client list entry so EWMH-aware tools see this window. */
  uint32_t pid = (uint32_t) getpid();
  set_prop32(win, intern_atom("_NET_WM_PID"), XCB_ATOM_CARDINAL, &pid, 1);

  xcb_map_window(conn, win);
  xcb_flush(conn);

  return win;
}

int
main(int argc, char** argv)
{
  int         count             = 1;
  const char* title             = "xclient";
  const char* klass             = "xclient";
  uint32_t    width             = 400;
  uint32_t    height            = 300;
  int         sleep_s           = 0;
  int         lifetime          = 0;
  bool        verbose           = false;
  bool        sync              = false;
  bool        override_redirect = false;

  int opt;
  while ((opt = getopt(argc, argv, "n:t:c:W:H:s:l:voyh")) != -1) {
    switch (opt) {
    case 'n':
      count = atoi(optarg);
      break;
    case 't':
      title = optarg;
      break;
    case 'c':
      klass = optarg;
      break;
    case 'W':
      width = (uint32_t) atoi(optarg);
      break;
    case 'H':
      height = (uint32_t) atoi(optarg);
      break;
    case 's':
      sleep_s = atoi(optarg);
      break;
    case 'l':
      lifetime = atoi(optarg);
      break;
    case 'v':
      verbose = true;
      break;
    case 'y':
      sync = true;
      break;
    case 'o':
      override_redirect = true;
      break;
    case 'h':
    default:
      printf("usage: xclient [-n COUNT] [-t TITLE] [-c CLASS] [-W WIDTH] "
             "[-H HEIGHT] [-s SLEEP] [-l LIFETIME] [-v] [-y] [-o] [-h]\n");
      return opt == 'h' ? 0 : 1;
    }
  }

  if (count < 1) {
    fprintf(stderr, "xclient: count must be >= 1\n");
    return 1;
  }

  int screen_num;
  conn = xcb_connect(NULL, &screen_num);
  if (xcb_connection_has_error(conn)) {
    fprintf(stderr, "xclient: cannot open display\n");
    return 1;
  }

  const xcb_setup_t*    setup = xcb_get_setup(conn);
  xcb_screen_iterator_t iter  = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < screen_num; i++)
    xcb_screen_next(&iter);
  screen = iter.data;
  if (screen == NULL) {
    fprintf(stderr, "xclient: no screen\n");
    return 1;
  }

  atom_wm_name     = XCB_ATOM_WM_NAME;
  atom_net_wm_name = intern_atom("_NET_WM_NAME");
  atom_utf8_string = intern_atom("UTF8_STRING");

  xcb_window_t* wins = calloc((size_t) count, sizeof(xcb_window_t));
  if (wins == NULL) {
    fprintf(stderr, "xclient: out of memory\n");
    return 1;
  }

  for (int i = 0; i < count; i++) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s-%d", title, i + 1);
    wins[i] = create_window(buf, klass, width, height, override_redirect);
    if (sync) {
      /* A request from this client is processed in order, so the reply proves
         the server has seen the map and redirected it. */
      free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), NULL));
    }
    if (verbose)
      printf("0x%08x %s\n", wins[i], buf);
    if (sleep_s > 0 && i + 1 < count)
      sleep((unsigned) sleep_s);
  }
  fflush(stdout);

  if (!verbose)
    printf("created %d window(s)\n", count);
  fflush(stdout);

  /*
   * Wait for events (and repaint on expose) so the windows stay mapped for
   * the duration of the test. Killed by the test script, or exits after
   * -l seconds when a bounded run is needed.
   */
  time_t deadline = lifetime > 0 ? time(NULL) + lifetime : 0;

  for (;;) {
    xcb_generic_event_t* ev = xcb_poll_for_event(conn);
    if (ev != NULL) {
      if ((ev->response_type & ~0x80) == XCB_EXPOSE) {
        xcb_expose_event_t* ex   = (xcb_expose_event_t*) ev;
        xcb_rectangle_t     rect = { 0, 0, (uint16_t) ex->width, (uint16_t) ex->height };
        xcb_poly_fill_rectangle(conn, ex->window, screen->white_pixel, 1, &rect);
        xcb_flush(conn);
      }
      free(ev);
      continue;
    }
    if (xcb_connection_has_error(conn))
      break;
    if (deadline != 0 && time(NULL) >= deadline)
      break;
    usleep(20000);
  }

  free(wins);
  xcb_disconnect(conn);
  return 0;
}
