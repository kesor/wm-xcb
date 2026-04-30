/*
 * Test Runner - Unified entry point for all wm-xcb tests
 *
 * This file includes all test headers to register their test groups.
 * The main() function is provided in test-registry.c.
 *
 * Usage: make test
 */

#include "test-registry.h"

/*
 * Include all test headers to register their test groups
 */
#include "test-wm-hub.h"
#include "test-wm-keybinding.h"
#include "test-wm-monitor.h"
#include "test-wm-tag.h"
#include "test-wm-window-list.h"
#include "test-wm-xcb-handler.h"
#include "test-wm.h"
/* Add more test headers as needed */