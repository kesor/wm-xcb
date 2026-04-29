/*
 * Test Registry - Unified test runner implementation
 *
 * Includes all test headers to register their test groups,
 * and provides main() to run all tests.
 *
 * Usage:
 *   // In any test file (e.g., test-wm-window-list.c)
 *   #include "test-registry.h"
 *
 *   TEST_GROUP(WindowList, {
 *     test_add_remove();
 *     test_iteration();
 *     // add more tests
 *   });
 */

#include <stdio.h>
#include <stdlib.h>
#include "test-registry.h"

/*
 * Include all test headers to register their test groups
 */
#include "test-wm.h"
#include "test-wm-window-list.h"
#include "test-wm-hub.h"
/* Add more test headers as needed */

/*
 * Registry linked list
 */
TestGroup* test_registry_head = NULL;
TestGroup* test_registry_tail = NULL;

/*
 * Result counters
 */
int tests_passed = 0;
int tests_failed = 0;

/*
 * Run all registered tests
 */
void
run_all_tests(void)
{
  printf("=== Running All Tests ===\n\n");

  TestGroup* group = test_registry_head;

  while (group != NULL) {
    printf("--- %s ---\n", group->name);

    int passed_before = tests_passed;
    int failed_before = tests_failed;
    group->run();

    int passed_this = tests_passed - passed_before;
    int failed_this = tests_failed - failed_before;

    printf("\n  [%s] %d passed, %d failed\n\n",
           failed_this > 0 ? "FAIL" : "PASS",
           passed_this,
           failed_this);

    group = group->next;
  }

  printf("=== Test Results ===\n");
  printf("  Total: %d passed, %d failed\n",
         tests_passed,
         tests_failed);

  if (tests_failed > 0) {
    printf("\n  *** SOME TESTS FAILED ***\n");
    exit(1);
  }
}

/*
 * Standalone main for the unified test runner
 */
int
main(void)
{
  run_all_tests();
  return 0;
}
