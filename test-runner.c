/*
 * Test Runner - Unified entry point for all wm-xcb tests
 *
 * This file includes all test headers to register their test groups.
 * The main() function is provided in test-registry.c.
 *
 * Note: This file is kept for historical reference. The actual test
 * binary is built from test-registry.c which includes all test headers.
 *
 * Usage: make test
 */

#include "test-registry.h"

/*
 * Include all test headers to register their test groups
 */
#include "test-wm.h"
#include "test-wm-window-list.h"
#include "test-wm-hub.h"
/* Add more test headers as needed */
