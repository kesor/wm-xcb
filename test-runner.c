/*
 * Test Runner - Unified entry point for all wm-xcb tests
 *
 * This file includes all test headers and runs all registered tests.
 * Individual test files should define TEST_GROUP() to register tests.
 *
 * Usage: make test
 */

#include <stdio.h>
#include "test-registry.h"

/*
 * Include all test headers to register their test groups
 */
#include "test-wm.h"
#include "test-wm-window-list.h"
#include "test-wm-hub.h"
/* test-wm-clients.h will be included once that test file exists */

/*
 * Standalone main for the unified test runner
 */
int
main(void)
{
  run_all_tests();
  return 0;
}
