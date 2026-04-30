/*
 * Monitor Tests
 */

#ifndef _TEST_WM_MONITOR_H_
#define _TEST_WM_MONITOR_H_

/*
 * Test groups for monitor functionality
 */
void test_monitor_create_destroy(void);
void test_monitor_list_init_shutdown(void);
void test_monitor_list_add_remove(void);
void test_monitor_list_iteration(void);
void test_monitor_find_by_output(void);
void test_monitor_selection(void);
void test_monitor_tag_operations(void);
void test_monitor_geometry(void);
void test_monitor_multiple_monitors(void);
void test_monitor_double_create(void);
void test_monitor_null_destroy(void);
void test_monitor_with_hub_integration(void);

#define TEST_GROUP_Monitor             \
  test_monitor_create_destroy();       \
  test_monitor_list_init_shutdown();   \
  test_monitor_list_add_remove();      \
  test_monitor_list_iteration();       \
  test_monitor_find_by_output();       \
  test_monitor_selection();            \
  test_monitor_tag_operations();       \
  test_monitor_geometry();             \
  test_monitor_multiple_monitors();    \
  test_monitor_double_create();        \
  test_monitor_null_destroy();         \
  test_monitor_with_hub_integration();

#endif /* _TEST_WM_MONITOR_H_ */
