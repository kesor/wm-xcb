#ifndef _WM_STATES_
#define _WM_STATES_

#include <xcb/xcb.h>

enum WindowType {
	WndTypeNone = XCB_NONE,
	WndTypeRoot,
	WndTypeClient,
	WndTypeTitle,
	WndTypeTagBar,
	WndTypeLast
};

enum ModKeyMask {
	KeyNone = XCB_NONE,
	KeyShift = XCB_KEY_BUT_MASK_SHIFT,
	KeyLock = XCB_KEY_BUT_MASK_LOCK,
	KeyMod1 = XCB_KEY_BUT_MASK_MOD_1,
	KeyMod2 = XCB_KEY_BUT_MASK_MOD_2,
	KeyMod3 = XCB_KEY_BUT_MASK_MOD_3,
	KeyMod4 = XCB_KEY_BUT_MASK_MOD_4,
	KeyMod5 = XCB_KEY_BUT_MASK_MOD_5,
	Btn1 = XCB_KEY_BUT_MASK_BUTTON_1,
	Btn2 = XCB_KEY_BUT_MASK_BUTTON_2,
	Btn3 = XCB_KEY_BUT_MASK_BUTTON_3,
	Btn4 = XCB_KEY_BUT_MASK_BUTTON_4,
	Btn5 = XCB_KEY_BUT_MASK_BUTTON_5
};

enum EventType {
	OnPress,
	OnRelease
};

enum State {
	STATE_IDLE,
	STATE_WINDOW_MOVE,
	STATE_WINDOW_RESIZE
};

struct StateTransition {
	enum WindowType window_target_type;
	enum ModKeyMask modkey_mask;
	enum EventType event_type;
	enum State prev_state;
	enum State next_state;
};

#define NUM_STATES 3
struct Context {
	enum State state;
	void (*state_funcs[NUM_STATES])(struct Context* ctx);

	/* set by the event handlers */
	enum WindowType window_target_type;
	enum ModKeyMask modkey_mask;
};

// adding more states -- add NUM_STATES above ^^^
void state_idle(struct Context* ctx);
void state_window_move(struct Context* ctx);
void state_window_resize(struct Context* ctx);

extern struct Context ctx;
void setup_state_machine();
void destruct_state_machine();
void handle_event(xcb_generic_event_t* event);

#endif
