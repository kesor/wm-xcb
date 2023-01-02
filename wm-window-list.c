#include <stdlib.h>
#include <xcb/xcb.h>

/*
 * The window manage holds only a small number of windows at any one time,
 * usually less than a hundred. Thus it is a good idea to optimize for
 * adding and removing windows as quickly as possible.
 *
 * Finding windows should also be fast to react to events that the X server
 * is sending about notifications or creation/destruction of windows.
 *
 **/

#include "wm-window-list.h"

wnd_node_t sentinel;

void window_properties_init(struct wnd_node_t* wnd) {
	wnd->title = NULL;
	wnd->parent = XCB_NONE;
	wnd->x = 0;
	wnd->y = 0;
	wnd->width = 0;
	wnd->height = 0;
	wnd->border_width = 0;
	wnd->override_redirect = 0;
	wnd->stack_mode = XCB_STACK_MODE_ABOVE;
}

void free_wnd_node(wnd_node_t* wnd) {
	free(wnd);
}

wnd_node_t* allocate_wnd_node(xcb_window_t window) {
	wnd_node_t* new_node = malloc(sizeof(wnd_node_t));
	new_node->window = window;
	window_properties_init(new_node);
	return new_node;
}

void window_insert(xcb_window_t window) {
	wnd_node_t* new_node = allocate_wnd_node(window);
	new_node->next = sentinel.next;
	new_node->prev = &sentinel;
	sentinel.next->prev = new_node;
	sentinel.next = new_node;
}

void window_remove(xcb_window_t window) {
	wnd_node_t* curr = &sentinel;
	while (curr->window != window)
		curr = curr->next;
	curr->prev->next = curr->next;
	curr->next->prev = curr->prev;
	free_wnd_node(curr);
}

wnd_node_t* window_find(xcb_window_t window) {
	wnd_node_t* curr = sentinel.next;
	while (curr != &sentinel) {
		if (curr->window == window)
			return curr;
		curr = curr->next;
	}
	return NULL;  // window not found
}

wnd_node_t* window_get_next(wnd_node_t* wnd) {
	return wnd->next;
}

wnd_node_t* window_get_prev(wnd_node_t* wnd) {
	return wnd->prev;
}

wnd_node_t* window_list_end() {
	return &sentinel;
}

void window_foreach(void (*callback)(wnd_node_t*)) {
	wnd_node_t* curr = &sentinel;
	while (curr != &sentinel) {
		callback(curr);
		curr = curr->next;
	}
}

void setup_window_list() {
	sentinel.prev = &sentinel;
	sentinel.next = &sentinel;
}

void destruct_window_list() {
	window_foreach(free_wnd_node);
}
