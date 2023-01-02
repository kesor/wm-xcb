#ifndef _WM_STATES_
#define _WM_STATES_

#include <xcb/xcb.h>

enum WindowType {
	WndTagBar,
	WndLtSymbol,
	WndStatusText,
	WndTitle,
	WndClient,
	WndRoot,
	WndLast
};

enum State {
	STATE_IDLE,
	STATE_WINDOW_MOVE,
	STATE_WINDOW_RESIZE,
	NUM_STATES
};

struct StateTransition {
	enum WindowType window_type;
	int mouse_button_state;
	int keyboard_key_state;
	enum State new_state;
};

struct Context {
	enum State state;
	void (*state_funcs[NUM_STATES])(struct Context* ctx);

	/* variables changeable by xcb event handlers */
	xcb_window_t hover_window;
	uint32_t mouse_buttons;
	uint32_t keyboard_keys;
};

extern struct Context ctx;
void setup_state_machine();
void destruct_state_machine();
void handle_event(xcb_generic_event_t* event);

#endif
