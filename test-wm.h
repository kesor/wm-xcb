#ifndef _TEST_WM_H_
#define _TEST_WM_H_

#include <stdlib.h>
#include "wm-log.h"

/*
 * Test assertion macro that tracks pass/fail counts
 * Requires test-registry.c to be linked in
 *
 * Note: Does NOT call exit() on failure - allows aggregated reporting
 * across all tests. Individual tests can still exit on critical failures.
 */
extern int tests_passed;
extern int tests_failed;

#define assert(EXPRESSION)                                            \
  do {                                                                \
    if (!(EXPRESSION)) {                                              \
      LOG_CLEAN("%s:%d: %s - FAIL", __FILE__, __LINE__, #EXPRESSION); \
      tests_failed++;                                                 \
    } else {                                                          \
      LOG_CLEAN("%s:%d: %s - pass", __FILE__, __LINE__, #EXPRESSION); \
      tests_passed++;                                                 \
    }                                                                 \
  } while (0)


#endif
