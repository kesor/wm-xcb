/*
 * test-wm-xcb-handler.h - Header for XCB handler tests
 */

#ifndef TEST_WM_XCB_HANDLER_H
#define TEST_WM_XCB_HANDLER_H

#include "src/xcb/xcb-handler.h"
#include "wm-hub.h"

/* Mock component for testing */
extern HubComponent mock_component;
extern HubComponent mock_component2;

/* Track handler calls */
extern int   handler_call_count;
extern void* last_handler_event;

typedef void (*test_handler_fn)(void*);

void test_xcb_handler_init_shutdown(void);
void test_register_single_handler(void);
void test_register_multiple_handlers_same_event(void);
void test_register_different_event_types(void);
void test_dispatch_calls_handlers(void);
void test_dispatch_multiple_handlers(void);
void test_dispatch_no_handler(void);
void test_dispatch_with_synthetic_event_flag(void);
void test_unregister_component(void);
void test_xcb_handler_unregister_nonexistent(void);
void test_unregister_null_component(void);
void test_lookup_returns_null_for_empty(void);
void test_next_returns_null_at_end(void);
void test_register_with_null_handler_fails(void);
void test_register_with_null_component_fails(void);
void test_dispatch_null_event(void);

#endif /* TEST_WM_XCB_HANDLER_H */