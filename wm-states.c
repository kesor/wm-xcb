#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "wm-states.h"
#include "config.def.h"


void state_idle(struct Context* ctx);
void state_window_move(struct Context* ctx);
void state_window_resize(struct Context* ctx);

struct Context ctx = {
	.state = STATE_IDLE,
	.hover_window = XCB_NONE,
	.mouse_buttons = 0,
	.keyboard_keys = 0,
	.state_funcs = {
		state_idle,
		state_window_move,
		state_window_resize,
	}
};

void set_state(struct Context* ctx, enum State new_state) {
	ctx->state = new_state;
}

void handle_event(xcb_generic_event_t* event) {
	switch (event->response_type) {
	case XCB_KEY_PRESS:
		break;
	case XCB_KEY_RELEASE:
		break;
	case XCB_BUTTON_PRESS:
		break;
	case XCB_BUTTON_RELEASE:
		break;
	case XCB_MOTION_NOTIFY:
		break;
	case XCB_ENTER_NOTIFY:
		break;
	case XCB_LEAVE_NOTIFY:
		break;
	case XCB_FOCUS_IN:
		break;
	case XCB_FOCUS_OUT:
		break;
	}

	// for (int i = 0; i < NUM_TRANSITIONS; i++) {
	// 	const struct StateTransition* t = &transitions[i];
	// 	if (t->event_type == event.type) {
	// 		bool match = false;
	// 		switch (event.type) {
	// 		case KEY_PRESSED:
	// 			match = t->key == event.key;
	// 			break;
	// 		case MOUSE_BUTTON_PRESSED:
	// 			match = t->mouse_button == event.mouse_button;
	// 			break;
	// 		case HOVER:
	// 			match = t->hover_target == event.hover_target;
	// 			break;
	// 			// other event types
	// 		}
	// 		if (match) {
	// 			set_state(ctx, t->new_state);
	// 			break;
	// 		}
	// 	}
	// }
}

void state_idle(struct Context* ctx) {
}

void state_window_move(struct Context* ctx) {
}

void state_window_resize(struct Context* ctx) {
}

void setup_state_machine() {
}

void destruct_state_machine() {
}
