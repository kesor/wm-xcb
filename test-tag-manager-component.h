/*
 * Tag Manager Component Tests
 *
 * Tests for the Tag Manager component implementation.
 */

#ifndef TEST_TAG_MANAGER_COMPONENT_H
#define TEST_TAG_MANAGER_COMPONENT_H

/*
 * Test: tag_manager_component_init_shutdown
 * Tests basic initialization and shutdown of tag manager.
 */
void test_tag_manager_init_shutdown(void);

/*
 * Test: tag_manager_receives_tag_view_requests
 * Tests that tag manager handles REQ_TAG_VIEW requests.
 */
void test_tag_manager_receives_tag_view_requests(void);

/*
 * Test: tag_manager_receives_tag_toggle_requests
 * Tests that tag manager handles REQ_TAG_TOGGLE requests.
 */
void test_tag_manager_receives_tag_toggle_requests(void);

/*
 * Test: tag_manager_emits_events
 * Tests that tag manager emits EVT_TAG_CHANGED on state changes.
 */
void test_tag_manager_emits_events(void);

/*
 * Test: tag_manager_updates_client_visibility
 * Tests that clients are shown/hidden based on tag membership.
 */
void test_tag_manager_updates_client_visibility(void);

/*
 * Test: tag_manager_client_tag_toggle
 * Tests moving focused client to/from tag.
 */
void test_tag_manager_client_tag_toggle(void);

/*
 * Test: tag_manager_with_multiple_monitors
 * Tests tag manager with multiple monitors.
 */
void test_tag_manager_with_multiple_monitors(void);

/*
 * Test: tag_manager_invalid_tag_index
 * Tests that invalid tag indices are rejected.
 */
void test_tag_manager_invalid_tag_index(void);

#endif /* TEST_TAG_MANAGER_COMPONENT_H */