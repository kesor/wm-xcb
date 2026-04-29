/*
 * Client List Tests
 *
 * Tests for the Client entity list implementation.
 * Tests the sentinel-based circular doubly-linked list.
 */

#include "test-registry.h"
#include "test-wm.h"
#include "src/target/client.h"

static int count = 0;

void
count_callback(Client* c)
{
  (void) c;
  count++;
}

void
test_add_remove()
{
  LOG_CLEAN("== Testing adding and removing clients from the list");

  hub_init();
  client_list_init();

  Client* c1 = client_create(1);
  Client* c2 = client_create(2);
  Client* c3 = client_create(3);

  assert(c1 != NULL);
  assert(c2 != NULL);
  assert(c3 != NULL);

  assert(client_get_by_window(1) == c1);
  assert(client_get_by_window(2) == c2);
  assert(client_get_by_window(3) == c3);

  assert(client_list_count() == 3);

  /* Destroy client 2 */
  client_destroy(c2);
  assert(client_get_by_window(1) == c1);
  assert(client_get_by_window(2) == NULL);
  assert(client_get_by_window(3) == c3);

  /* Destroy clients 1 and 3 */
  client_destroy(c1);
  client_destroy(c3);

  assert(client_list_count() == 0);
  hub_shutdown();
}

void
test_iteration()
{
  LOG_CLEAN("== Testing iteration over the list");

  hub_init();
  client_list_init();

  count = 0;
  client_foreach(count_callback);
  assert(count == 0);

  client_create(1);
  client_create(2);
  client_create(3);

  count = 0;
  client_foreach(count_callback);
  assert(count == 3);

  hub_shutdown();
}

void
test_reverse_iteration()
{
  LOG_CLEAN("== Testing reverse iteration over the list");

  hub_init();
  client_list_init();

  client_create(1);
  client_create(2);
  client_create(3);

  /* Count in reverse */
  count = 0;
  client_foreach_reverse(count_callback);
  assert(count == 3);

  hub_shutdown();
}

void
test_remove_non_existent_client()
{
  LOG_CLEAN("== Testing removal of non-existent client");

  hub_init();
  client_list_init();

  client_create(1);

  /* Try to get non-existent client */
  assert(client_get_by_window(2) == NULL);
  assert(client_list_count() == 1);

  hub_shutdown();
}

void
test_empty_list()
{
  LOG_CLEAN("== Testing empty list");

  hub_init();
  client_list_init();

  assert(client_list_count() == 0);
  assert(client_list_is_empty() == true);
  assert(client_list_get_head() == NULL);

  hub_shutdown();
}

void
test_client_get_by_window()
{
  LOG_CLEAN("== Testing client_get_by_window");

  hub_init();
  client_list_init();

  Client* c1 = client_create(1);
  Client* c2 = client_create(2);
  Client* c3 = client_create(3);

  assert(client_get_by_window(1) == c1);
  assert(client_get_by_window(2) == c2);
  assert(client_get_by_window(3) == c3);
  assert(client_get_by_window(4) == NULL);

  hub_shutdown();
}

void
test_client_create_invalid()
{
  LOG_CLEAN("== Testing client_create with invalid window");

  hub_init();
  client_list_init();

  /* Create with XCB_NONE should fail */
  Client* c = client_create(XCB_NONE);
  assert(c == NULL);
  assert(client_list_count() == 0);

  hub_shutdown();
}

void
test_client_create_duplicate()
{
  LOG_CLEAN("== Testing client_create with duplicate window");

  hub_init();
  client_list_init();

  Client* c1 = client_create(1);
  assert(c1 != NULL);

  /* Creating same window again should return NULL (already registered) */
  Client* c2 = client_create(1);
  assert(c2 == NULL); /* Should fail - already exists */
  assert(client_list_count() == 1);

  hub_shutdown();
}

void
test_client_destroy_by_window()
{
  LOG_CLEAN("== Testing client_destroy_by_window");

  hub_init();
  client_list_init();

  client_create(1);
  client_create(2);
  assert(client_list_count() == 2);

  client_destroy_by_window(1);
  assert(client_list_count() == 1);
  assert(client_get_by_window(1) == NULL);

  hub_shutdown();
}

void
test_client_list_contains_window()
{
  LOG_CLEAN("== Testing client_list_contains_window");

  hub_init();
  client_list_init();

  assert(client_list_contains_window(1) == false);

  client_create(1);
  assert(client_list_contains_window(1) == true);
  assert(client_list_contains_window(2) == false);

  hub_shutdown();
}

TEST_GROUP(ClientList, {
  test_add_remove();
  test_iteration();
  test_reverse_iteration();
  test_remove_non_existent_client();
  test_empty_list();
  test_client_get_by_window();
  test_client_create_invalid();
  test_client_create_duplicate();
  test_client_destroy_by_window();
  test_client_list_contains_window();
});
