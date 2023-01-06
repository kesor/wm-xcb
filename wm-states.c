#include <string.h>
#include <xcb/xcb.h>

#include "wm-log.h"
#include "wm-xcb.h"
#include "wm-states.h"
#include "config.def.h"

void state_idle(struct Context* ctx);
void state_window_move(struct Context* ctx);
void state_window_resize(struct Context* ctx);

const char* event_type_name(enum EventType event_type) {
	switch (event_type) {
	case EventTypeNone: return "EventTypeNone";
	case EventTypeKeyPress: return "EventTypeKeyPress";
	case EventTypeKeyRelease: return "EventTypeKeyRelease";
	case EventTypeBtnPress: return "EventTypeBtnPress";
	case EventTypeBtnRelease: return "EventTypeBtnRelease";
	default: return "INVALID";
	}
}


#define MOD_KEY_NAME_BUF_SIZE 80

const char* mod_key_name(enum ModKey mod_key) {
	static char buf[MOD_KEY_NAME_BUF_SIZE];
	buf[0] = '\0';

	if (mod_key & KeyShift)
		strcat(buf, "KeyShift|");
	if (mod_key & KeyLock)
		strcat(buf, "KeyLock|");
	if (mod_key & KeyMod1)
		strcat(buf, "KeyMod1|");
	if (mod_key & KeyMod2)
		strcat(buf, "KeyMod2|");
	if (mod_key & KeyMod3)
		strcat(buf, "KeyMod3|");
	if (mod_key & KeyMod4)
		strcat(buf, "KeyMod4|");
	if (mod_key & KeyMod5)
		strcat(buf, "KeyMod5|");
	if (mod_key & Btn1)
		strcat(buf, "Btn1|");
	if (mod_key & Btn2)
		strcat(buf, "Btn2|");
	if (mod_key & Btn3)
		strcat(buf, "Btn3|");
	if (mod_key & Btn4)
		strcat(buf, "Btn4|");
	if (mod_key & Btn5)
		strcat(buf, "Btn5|");

	// remove the trailing '|' character
	int len = strlen(buf);
	if (len > 0)
		buf[len - 1] = '\0';
	else
		strcpy(buf, "INVALID");

	return buf;
}

struct Context ctx = {
	.state = STATE_IDLE,
	.window_target_type = WndTypeNone,
	.event_type = EventTypeNone,
	.key_mod = KeyNone,
	.key = XCB_NONE,
	.btn_mod = KeyNone,
	.btn = XCB_NONE,
	.root = XCB_NONE,
	.root_x = XCB_NONE,
	.root_y = XCB_NONE,
	.child = XCB_NONE,
	.event_x = XCB_NONE,
	.event_y = XCB_NONE,
	.state_funcs = {
		state_idle,
		state_window_move,
		state_window_resize,
	}
};

void set_state(struct Context* ctx, enum State new_state) {
	ctx->state = new_state;
}

void handle_state_event(xcb_generic_event_t* event) {
	xcb_key_press_event_t* key_event;
	xcb_button_press_event_t* btn_event;
	// xcb_window_t prev_wnd = XCB_NONE;
	xcb_window_t hover_wnd = XCB_NONE;

	uint8_t handle_this_event = 0;

	enum EventType type = EventTypeNone;

	switch (event->response_type) {
	case XCB_KEY_RELEASE:
		type = EventTypeKeyRelease;
	case XCB_KEY_PRESS:
		key_event = (xcb_key_press_event_t*)event;
		if (type == EventTypeNone)
			type = EventTypeKeyPress;
		ctx.event_type = type;
		ctx.key_mod = (enum ModKey)key_event->state;
		ctx.key = key_event->detail;
		ctx.root = key_event->root;
		ctx.root_x = key_event->root_x;
		ctx.root_y = key_event->root_y;
		ctx.child = key_event->child;
		if (key_event->same_screen) {
			ctx.event_x = key_event->event_x;
			ctx.event_y = key_event->event_y;
		}
		handle_this_event = 1;
		break;

	case XCB_BUTTON_RELEASE:
		type = EventTypeBtnRelease;
	case XCB_BUTTON_PRESS:
		btn_event = (xcb_button_press_event_t*)event;
		if (type == EventTypeNone)
			type = EventTypeBtnPress;
		ctx.event_type = type;
		ctx.btn_mod = (enum ModKey)btn_event->state;
		ctx.btn = btn_event->detail;
		ctx.root = btn_event->root;
		ctx.root_x = btn_event->root_x;
		ctx.root_y = btn_event->root_y;
		ctx.child = btn_event->child;
		if (btn_event->same_screen) {
			ctx.event_x = btn_event->event_x;
			ctx.event_y = btn_event->event_y;
		}
		handle_this_event = 1;
		break;
	case XCB_MOTION_NOTIFY:
		break;
	case XCB_ENTER_NOTIFY:
		hover_wnd = ((xcb_enter_notify_event_t*)event)->event;
		break;
	case XCB_LEAVE_NOTIFY:
		// prev_wnd = ((xcb_leave_notify_event_t*)event)->event;
		break;
	case XCB_FOCUS_IN:
		break;
	case XCB_FOCUS_OUT:
		break;
	}

	ctx.window_target_type = WndTypeNone;
	if (hover_wnd == root)
		ctx.window_target_type = WndTypeRoot;
	else if (hover_wnd != XCB_NONE)
		ctx.window_target_type = WndTypeClient;

	if (!handle_this_event)
		return;

	LOG_DEBUG("Finding transition for event, with following properties:\n\troot: %d\n\tchild:%d\n\twnd_target_type: %d\n\tevent_type: %s\n\tkey_mod: %s\n\tkey: %d\n\tbtn_mod: %s\n\tbtn: %d\n\tstate: %d",
		ctx.root,
		ctx.child,
		ctx.window_target_type,
		event_type_name(ctx.event_type),
		mod_key_name(ctx.key_mod),
		ctx.key,
		mod_key_name(ctx.btn_mod),
		ctx.btn,
		ctx.state
	);

	int num_transitions = sizeof(transitions) / sizeof(transitions[0]);
	for (int i = 0; i < num_transitions; i++) {
		struct StateTransition t = transitions[i];
		if (t.window_target_type == ctx.window_target_type && t.event_type == ctx.event_type && t.prev_state == ctx.state) {
			if ((ctx.event_type == EventTypeKeyPress || ctx.event_type == EventTypeKeyRelease) && t.mod == ctx.key_mod && t.key == ctx.key) {
				ctx.state = t.next_state;
				break;
			}
			if ((ctx.event_type == EventTypeBtnPress || ctx.event_type == EventTypeBtnRelease) && t.mod == ctx.btn_mod && t.btn == ctx.btn) {
				ctx.state = t.next_state;
				break;
			}
		}
	}

	ctx.state_funcs[ctx.state](&ctx);
}

void state_idle(struct Context* ctx) {
	// LOG_DEBUG("State: Idle.");
}

void state_window_move(struct Context* ctx) {
	LOG_DEBUG("Action: Window movement.");
}

void state_window_resize(struct Context* ctx) {
	LOG_DEBUG("Action: Window resizing.");
}

void setup_state_machine() {
}

void destruct_state_machine() {
}
