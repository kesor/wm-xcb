/*
 * Test Registry - Unified test runner for wm-xcb
 *
 * Each test file includes this header and calls TEST_GROUP() to register
 * its test functions. The run_all_tests() function executes all registered tests.
 *
 * Usage:
 *   // In any test file (e.g., test-wm-window-list.c)
 *   #include "test-registry.h"
 *
 *   TEST_GROUP(WindowList, {
 *     test_add_remove();
 *     test_iteration();
 *     test_window_find();
 *     // ...
 *   });
 */

#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

#include <stdio.h>
#include <stdlib.h>

/*
 * Test group structure
 */
typedef struct TestGroup {
  const char* name;
  void (*run)(void);
  struct TestGroup* next;
} TestGroup;

/*
 * Registry head - linked list of test groups
 */
extern TestGroup* test_registry_head;
extern TestGroup* test_registry_tail;

/*
 * Register a test group - use constructor attribute for auto-registration
 */
#define TEST_GROUP(NAME, BODY)                                       \
  static void test_group_##NAME##_runner(void)                       \
  {                                                                  \
    BODY;                                                            \
  }                                                                  \
  __attribute__((constructor)) void register_test_group_##NAME(void) \
  {                                                                  \
    static TestGroup group = {                                       \
      .name = #NAME,                                                 \
      .run  = test_group_##NAME##_runner,                            \
      .next = NULL,                                                  \
    };                                                               \
    if (test_registry_head == NULL) {                                \
      test_registry_head = test_registry_tail = &group;              \
    } else {                                                         \
      test_registry_tail->next = &group;                             \
      test_registry_tail       = &group;                             \
    }                                                                \
  }

/*
 * Run all registered tests
 */
void run_all_tests(void);

/*
 * Test result counters (extern for test files)
 */
extern int tests_passed;
extern int tests_failed;

#endif /* TEST_REGISTRY_H */
