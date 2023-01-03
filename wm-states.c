#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "wm-states.h"
#include "config.def.h"

void state_idle(struct Context* ctx);
void state_window_move(struct Context* ctx);
void state_window_resize(struct Context* ctx);

struct Context ctx = {
	.state = STATE_IDLE,
	.window_target_type = WndTypeNone,
	.modkey_mask = KeyNone,
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

	for (int i = 0; i < sizeof(transitions) / sizeof(transitions[0]); i++) {
		const struct StateTransition* t = &transitions[i];
		if (t->window_target_type == ctx.window_target_type &&
			t->modkey_mask == ctx.modkey_mask &&
			t->event_type == event->response_type &&
			t->prev_state == ctx.state) {
			set_state(&ctx, t->next_state);
			break;
		}
	}

	ctx.state_funcs[ctx.state](&ctx);

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
