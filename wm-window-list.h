#ifndef _WM_WINDOW_LIST_
#define _WM_WINDOW_LIST_

#include <xcb/xcb.h>

typedef struct wnd_node_t {
	struct wnd_node_t* next;
	struct wnd_node_t* prev;

	char* title;
	xcb_window_t parent;
	xcb_window_t window;
	int16_t      x;
	int16_t      y;
	uint16_t     width;
	uint16_t     height;
	uint16_t     border_width;
	uint8_t      override_redirect;
	uint8_t      stack_mode;
} wnd_node_t;

void setup_window_list();
void destruct_window_list();
void free_wnd_node(wnd_node_t* wnd);
void window_remove(xcb_window_t window);
wnd_node_t* window_insert(xcb_window_t window);
wnd_node_t* window_find(xcb_window_t window);
wnd_node_t* window_get_next(wnd_node_t* wnd);
wnd_node_t* window_get_prev(wnd_node_t* wnd);
wnd_node_t* window_list_end();
void window_foreach(void (*callback)(wnd_node_t*));
wnd_node_t* get_wnd_list_sentinel();

#endif
