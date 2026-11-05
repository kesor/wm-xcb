/*
 * EWMH Desktop Component Tests
 *
 * The component turns the tag model into the values EWMH root and window
 * properties carry, and a pager's desktop request back into a tag request.
 * The unit suite has no X connection, so the properties themselves are checked
 * in tests/x/test-60-ewmh-root-properties.sh; what is checked here is the
 * conversion, the registration, and the request the message handler sends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "config.h"
#include "src/actions/action-registry.h"
#include "src/components/ewmh-desktop.h"
#include "src/components/tag-manager.h"
#include "src/target/client.h"
#include "src/target/monitor.h"
#include "src/target/tag.h"
#include "src/xcb/xcb-handler.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"
#include "wm-xcb-ewmh.h"

/*
 * Test: a client on exactly one tag names that desktop; one on several or on
 * none reports "all desktops", which is the only other value EWMH has.
 */
void
test_desktop_for_tags(void)
{
  LOG_CLEAN("== Testing the desktop a client's tags name");

  assert(ewmh_desktop_for_client_tags(TAG_MASK(0)) == 0);
  assert(ewmh_desktop_for_client_tags(TAG_MASK(4)) == 4);
  assert(ewmh_desktop_for_client_tags(TAG_MASK(8)) == 8);

  assert(ewmh_desktop_for_client_tags(TAG_MASK(0) | TAG_MASK(5)) == EWMH_DESKTOP_ALL);
  assert(ewmh_desktop_for_client_tags(TAG_ALL_TAGS) == EWMH_DESKTOP_ALL);
  assert(ewmh_desktop_for_client_tags(0) == EWMH_DESKTOP_ALL);
}

/*
 * Test: the current desktop is the lowest visible tag, and a view with no tag
 * on it still reports a desktop a panel can draw.
 */
void
test_current_desktop_from_visible_tags(void)
{
  LOG_CLEAN("== Testing the current desktop index");

  assert(ewmh_desktop_for_visible_tags(TAG_MASK(0)) == 0);
  assert(ewmh_desktop_for_visible_tags(TAG_MASK(4)) == 4);
  assert(ewmh_desktop_for_visible_tags(TAG_MASK(2) | TAG_MASK(7)) == 2);
  assert(ewmh_desktop_for_visible_tags(TAG_ALL_TAGS) == 0);
  assert(ewmh_desktop_for_visible_tags(0) == 0);
}

/*
 * Test: the desktop-name blob is the tag number for a tag the config file did
 * not name, and the configured name for one it did.
 */
void
test_desktop_names_blob(void)
{
  LOG_CLEAN("== Testing the desktop name blob");

  config_shutdown();

  size_t len = ewmh_desktop_names(NULL, 0);
  /* "1" through "9", each with its terminating NUL. */
  assert(len == 2 * TAG_NUM_TAGS);

  char blob[2 * TAG_NUM_TAGS];
  memset(blob, 0, sizeof blob);
  assert(ewmh_desktop_names(blob, sizeof blob) == len);
  assert(blob[0] == '1');
  assert(blob[1] == '\0');
  assert(blob[2] == '2');
  assert(blob[3] == '\0');
  /* The list ends with the last name's NUL, not with a stray byte. */
  assert(blob[len - 1] == '\0');
  assert(blob[len - 2] == '9');
}

/*
 * Test: a short buffer keeps every name that fits whole and reports the size
 * the whole list needs.
 */
void
test_desktop_names_short_buffer(void)
{
  LOG_CLEAN("== Testing a short desktop name buffer");

  config_shutdown();

  char blob[5];
  memset(blob, 'x', sizeof blob);

  size_t needed = ewmh_desktop_names(blob, sizeof blob);
  assert(needed == 2 * TAG_NUM_TAGS);

  /* "1\0" and "2\0" fit in four bytes; the fifth is left as it was. */
  assert(blob[0] == '1');
  assert(blob[1] == '\0');
  assert(blob[2] == '2');
  assert(blob[3] == '\0');
  assert(blob[4] == 'x');
}

/*
 * Write a config file naming the first two tags, so the blob has to come from
 * the file rather than from the tag numbers.
 */
static bool
write_tag_config(char* path, size_t size)
{
  const char* dir = getenv("TMPDIR");
  int         fd;
  FILE*       file;

  if (dir == NULL || dir[0] == '\0')
    dir = "/tmp";
  if (snprintf(path, size, "%s/wm-ewmh-test-XXXXXX", dir) >= (int) size)
    return false;

  fd = mkstemp(path);
  if (fd < 0)
    return false;

  file = fdopen(fd, "w");
  if (file == NULL) {
    close(fd);
    unlink(path);
    return false;
  }

  (void) fputs("tags work chat\n", file);
  (void) fclose(file);
  return true;
}

/*
 * Test: a tag the config file named is published under that name.
 */
void
test_desktop_names_from_the_config_file(void)
{
  LOG_CLEAN("== Testing desktop names from the config file");

  char path[256];
  assert_or_abort(write_tag_config(path, sizeof path));
  assert(config_load_runtime(path) == true);

  char blob[64];
  memset(blob, 0, sizeof blob);
  assert(ewmh_desktop_names(blob, sizeof blob) == strlen("work") + 1 + strlen("chat") + 1 + 7 * 2);
  assert(strcmp(blob, "work") == 0);
  assert(strcmp(blob + strlen("work") + 1, "chat") == 0);

  config_shutdown();
  unlink(path);
}

/*
 * Test: the component registers itself, its ClientMessage handler, and drops
 * both again on shutdown.
 */
void
test_ewmh_desktop_starts_and_stops(void)
{
  LOG_CLEAN("== Testing the EWMH desktop component starts and stops");

  hub_init();
  xcb_handler_init();
  tag_list_init();
  monitor_list_init();

  assert(ewmh_desktop_component_init() == true);
  assert(ewmh_desktop_component_is_initialized() == true);
  assert(hub_get_component_by_name(EWMH_DESKTOP_COMPONENT_NAME) != NULL);
  assert(xcb_handler_count_for_type(XCB_CLIENT_MESSAGE) >= 1);

  ewmh_desktop_component_shutdown();

  assert(ewmh_desktop_component_is_initialized() == false);
  assert(hub_get_component_by_name(EWMH_DESKTOP_COMPONENT_NAME) == NULL);
  assert(xcb_handler_count_for_type(XCB_CLIENT_MESSAGE) == 0);

  monitor_list_shutdown();
  tag_list_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

/*
 * Test: a pager's _NET_CURRENT_DESKTOP message switches the tag view through
 * the tag manager, and an impossible request changes nothing.
 */
void
test_client_message_switches_the_tag_view(void)
{
  LOG_CLEAN("== Testing a desktop request reaches the tag manager");

  hub_init();
  xcb_handler_init();
  tag_list_init();
  monitor_list_init();
  action_registry_init();
  tag_manager_component_init();

  Monitor* m = monitor_create(100);
  assert_or_abort(m != NULL);
  assert_or_abort(tag_manager_get_view_sm(m) != NULL);

  /* The atoms come from the EWMH connection, which a unit run has no server
   * for; the handler only compares the message type against this value. */
  xcb_ewmh_connection_t  fake;
  xcb_ewmh_connection_t* saved_ewmh = ewmh;
  memset(&fake, 0, sizeof fake);
  fake._NET_CURRENT_DESKTOP = 4242;
  ewmh                      = &fake;

  xcb_client_message_event_t message;
  memset(&message, 0, sizeof message);
  message.type           = 4242;
  message.data.data32[0] = 1;
  ewmh_desktop_on_client_message(&message);
  assert((tag_manager_get_visible_tags(m) & TAG_MASK(1)) != 0);
  assert((tag_manager_get_visible_tags(m) & TAG_MASK(0)) == 0);

  /* A request for a desktop that does not exist, a message of another type,
   * and no message at all: the view stays where the last real request left it. */
  uint32_t before = tag_manager_get_visible_tags(m);

  memset(&message, 0, sizeof message);
  message.type           = 4242;
  message.data.data32[0] = TAG_NUM_TAGS;
  ewmh_desktop_on_client_message(&message);
  assert(tag_manager_get_visible_tags(m) == before);

  memset(&message, 0, sizeof message);
  message.type           = 4243;
  message.data.data32[0] = 0;
  ewmh_desktop_on_client_message(&message);
  assert(tag_manager_get_visible_tags(m) == before);

  ewmh_desktop_on_client_message(NULL);
  assert(tag_manager_get_visible_tags(m) == before);

  ewmh = saved_ewmh;

  tag_manager_component_shutdown();
  action_registry_shutdown();
  monitor_destroy(m);
  monitor_list_shutdown();
  tag_list_shutdown();
  xcb_handler_shutdown();
  hub_shutdown();
}

TEST_GROUP(EwmhDesktop, {
  test_desktop_for_tags();
  test_current_desktop_from_visible_tags();
  test_desktop_names_blob();
  test_desktop_names_short_buffer();
  test_desktop_names_from_the_config_file();
  test_ewmh_desktop_starts_and_stops();
  test_client_message_switches_the_tag_view();
});
