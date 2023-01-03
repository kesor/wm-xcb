#include "test-wm.h"
#include "wm-window-list.h"

static int count = 0;
static wnd_node_t* sentinel;

void count_callback(wnd_node_t* wnd) {
	count++;
}

void test_add_remove() {
	LOG_CLEAN("== Testing adding and removing nodes from the list");
	setup_window_list();
	window_insert(1);
	window_insert(2);
	window_insert(3);
	assert(window_find(1) != NULL);
	assert(window_find(2) != NULL);
	assert(window_find(3) != NULL);
	window_remove(2);
	assert(window_find(1) != NULL);
	assert(window_find(2) == NULL);
	assert(window_find(3) != NULL);
	window_remove(1);
	window_remove(3);
	assert(window_find(1) == NULL);
	assert(window_find(2) == NULL);
	assert(window_find(3) == NULL);
	destruct_window_list();
}

void test_iteration() {
	LOG_CLEAN("== Testing iteration over the list");
	setup_window_list();

	count = 0;
	window_foreach(count_callback);
	assert(count == 0);

	window_insert(1);
	window_insert(2);
	window_insert(3);
	window_foreach(count_callback);
	assert(count == 3);

	destruct_window_list();
}

void test_remove_non_existent_window() {
	LOG_CLEAN("== Testing removal of non-existent window");
	setup_window_list();
	window_insert(1);
	window_remove(2); // try to remove window 2 from an empty list
	assert(window_find(2) == NULL);
	destruct_window_list();
}

void test_empty_list() {
	LOG_CLEAN("== Testing empty list");
	setup_window_list();
	sentinel = get_wnd_list_sentinel();
	assert(window_find(1) == NULL);
	assert(sentinel->next == sentinel);
	assert(sentinel->prev == sentinel);
	destruct_window_list();
}

void test_window_find() {
	LOG_CLEAN("== Testing window_find");
	setup_window_list();
	window_insert(1);
	window_insert(2);
	window_insert(3);
	assert(window_find(1) != NULL);
	assert(window_find(2) != NULL);
	assert(window_find(3) != NULL);
	assert(window_find(4) == NULL);
	destruct_window_list();
}

void test_window_insert_invalid() {
	LOG_CLEAN("== Testing window_insert with XCB_NONE");
	setup_window_list();
	assert(window_insert(XCB_NONE) == XCB_NONE);
	assert(window_find(XCB_NONE) == NULL);
	assert(sentinel->next == sentinel);
	assert(sentinel->prev == sentinel);
	destruct_window_list();
}

void test_window_remove_invalid() {
	LOG_CLEAN("== Testing window_remove with invalid window XCB_NONE");
	setup_window_list();
	window_remove(XCB_NONE);
	assert(sentinel->next == sentinel);
	assert(sentinel->prev == sentinel);
	destruct_window_list();
}

void test_window_insert_duplicate() {
	LOG_CLEAN("== Testing window_insert with duplicate window handle");
	setup_window_list();
	xcb_window_t window = 1;
	window_insert(window);
	assert(window_find(window) != NULL);
	assert(sentinel->next != sentinel);
	assert(sentinel->prev != sentinel);
	// Insert the same window handle again
	window_insert(window);

	// Check that the window handle has been updated in the existing node, rather than creating a new node
	assert(window_find(window) != NULL);
	assert(sentinel->next != sentinel);
	assert(sentinel->prev != sentinel);
	assert(sentinel->next == sentinel->prev);  // there should only be one node in the list
	destruct_window_list();
}

void test_window_insert_returns_node() {
	LOG_CLEAN("== Testing window_insert returning the window object");
	setup_window_list();
	xcb_window_t window = 123;
	wnd_node_t* node = window_insert(window);
	assert(node->window == window);
	assert(window_find(window) == node);
	destruct_window_list();
}

int main() {
	test_add_remove();
	test_iteration();
	test_remove_non_existent_window();
	test_empty_list();
	test_window_find();
	test_window_insert_invalid();
	test_window_remove_invalid();
	test_window_insert_duplicate();
	test_window_insert_returns_node();
}
