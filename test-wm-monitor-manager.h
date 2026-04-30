/*
 * Monitor Manager Tests
 */

#ifndef _TEST_WM_MONITOR_MANAGER_H_
#define _TEST_WM_MONITOR_MANAGER_H_

/*
 * Test groups for monitor manager functionality
 */
void test_monitor_manager_component_init_shutdown(void);
void test_monitor_manager_handler_registration(void);
void test_monitor_manager_multiple_init(void);
void test_monitor_manager_no_requests(void);
void test_monitor_manager_accepts_monitor_target(void);
void test_monitor_manager_with_monitors(void);

#define TEST_GROUP_MonitorManager                 \
  test_monitor_manager_component_init_shutdown(); \
  test_monitor_manager_handler_registration();    \
  test_monitor_manager_multiple_init();           \
  test_monitor_manager_no_requests();             \
  test_monitor_manager_accepts_monitor_target();  \
  test_monitor_manager_with_monitors();

#endif /* _TEST_WM_MONITOR_MANAGER_H_ */
