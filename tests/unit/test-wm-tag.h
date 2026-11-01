/*
 * Tag Tests
 *
 * Tests for the Tag entity implementation.
 * Requires: hub
 */

#ifndef TEST_WM_TAG_H
#define TEST_WM_TAG_H

/*
 * Test: tag_create_destroy
 * Tests creating and destroying a single tag.
 */
void test_tag_create_destroy(void);

/*
 * Test: tag_list_init_shutdown
 * Tests tag list initialization with default tags.
 */
void test_tag_list_init_shutdown(void);

/*
 * Test: tag_list_iteration
 * Tests iterating over all tags.
 */
void test_tag_list_iteration(void);

/*
 * Test: tag_get_by_index
 * Tests getting a tag by its index.
 */
void test_tag_get_by_index(void);

/*
 * Test: tag_get_by_name
 * Tests getting a tag by its name.
 */
void test_tag_get_by_name(void);

/*
 * Test: tag_target_id_conversion
 * Tests conversion between tag indices and TargetIDs.
 */
void test_tag_target_id_conversion(void);

/*
 * Test: tag_with_hub_integration
 * Tests tag integration with the Hub registry.
 */
void test_tag_with_hub_integration(void);

/*
 * Test: tag_iterate_by_mask
 * Tests iterating over tags matching a bitmask.
 */
void test_tag_iterate_by_mask(void);

/*
 * Test: tag_multiple_monitors_reference
 * Tests that multiple monitors can reference tags.
 */
void test_tag_multiple_monitors_reference(void);

#endif /* TEST_WM_TAG_H */
