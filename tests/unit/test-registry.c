/*
 * Test Registry - Unified test runner implementation
 *
 * Includes all test headers to register their test groups,
 * and provides main() to run all tests.
 *
 * Usage:
 *   // In any test file (e.g., test-wm-window-list.c)
 *   #include "tests/unit/test-registry.h"
 *
 *   TEST_GROUP(WindowList, {
 *     test_add_remove();
 *     test_iteration();
 *     // add more tests
 *   });
 */

#include "tests/unit/test-registry.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Include all test headers to register their test groups
 */
#include "tests/unit/test-client-list-component.h"
#include "tests/unit/test-focus-component.h"
#include "tests/unit/test-launcher.h"
#include "tests/unit/test-target-client.h"
#include "tests/unit/test-terminal.h"
#include "tests/unit/test-wm-hub.h"
#include "tests/unit/test-wm-monitor-manager.h"
#include "tests/unit/test-wm-monitor.h"
#include "tests/unit/test-wm-xcb-handler.h"
#include "tests/unit/test-wm.h"

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
  /*
   * Line-buffered stdout. The suite is usually run into a pipe (`make test |
   * tail`, a CI log, this sanitizer check), and LeakSanitizer exits the process
   * without flushing a block buffer: the assertion summary would vanish on
   * exactly the runs where the leak report matters.
   */
  setvbuf(stdout, NULL, _IOLBF, 0);
  run_all_tests();
  return 0;
}
