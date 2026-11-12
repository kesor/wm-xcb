/*
 * Test Runner - Unified entry point for all wm-xcb tests
 *
 * This file includes all test headers to register their test groups.
 * The main() function is provided in test-registry.c.
 *
 * Usage: make test
 */

#include "tests/unit/test-registry.h"

/*
 * Include all test headers to register their test groups
 */
#include "tests/unit/test-launcher.h"
#include "tests/unit/test-terminal.h"
#include "tests/unit/test-wm-hub.h"
#include "tests/unit/test-wm-keybinding.h"
#include "tests/unit/test-wm-monitor.h"
#include "tests/unit/test-wm-tag.h"
#include "tests/unit/test-wm-xcb-handler.h"
#include "tests/unit/test-wm.h"
/* Add more test headers as needed */