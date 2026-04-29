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
void test_monitor_tiling_params(void);
void test_monitor_client_list(void);
void test_monitor_multiple_monitors(void);
void test_monitor_double_create(void);
void test_monitor_double_destroy(void);
void test_monitor_with_hub_integration(void);

#define TEST_GROUP_Monitor \\\n  test_monitor_create_destroy(); \\\n  test_monitor_list_init_shutdown(); \\\n  test_monitor_list_add_remove(); \\\n  test_monitor_list_iteration(); \\\n  test_monitor_find_by_output(); \\\n  test_monitor_selection(); \\\n  test_monitor_tag_operations(); \\\n  test_monitor_geometry(); \\\n  test_monitor_tiling_params(); \\\n  test_monitor_client_list(); \\\n  test_monitor_multiple_monitors(); \\\n  test_monitor_double_create(); \\\n  test_monitor_double_destroy(); \\\n  test_monitor_with_hub_integration();

#endif /* _TEST_WM_MONITOR_H_ */
