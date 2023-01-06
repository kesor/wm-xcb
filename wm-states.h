#ifndef _WM_STATES_H_
#define _WM_STATES_H_

#include <xcb/xcb.h>

#define XK_LATIN1
#include <X11/keysymdef.h>

enum WindowType {
	WndTypeNone = XCB_NONE,
	WndTypeRoot,
	WndTypeClient,
	WndTypeTitle,
	WndTypeTagBar,
	WndTypeLast
};

enum ModKey {
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
	EventTypeNone = XCB_NONE,
	EventTypeKeyPress,
	EventTypeKeyRelease,
	EventTypeBtnPress,
	EventTypeBtnRelease
};

enum State {
	STATE_IDLE,
	STATE_WINDOW_MOVE,
	STATE_WINDOW_RESIZE
};

struct StateTransition {
	enum WindowType window_target_type;
	enum EventType event_type;
	enum ModKey mod;
	xcb_button_t btn;
	xcb_keycode_t key;
	enum State prev_state;
	enum State next_state;
};

#define NUM_STATES 3

struct Context {
	enum State state;
	void (*state_funcs[NUM_STATES])(struct Context* ctx);

	/* set by the event handlers */
	enum WindowType window_target_type;
	enum EventType  event_type;
	enum ModKey     key_mod;
	xcb_keycode_t   key;
	enum ModKey     btn_mod;
	xcb_button_t    btn;
	xcb_window_t    root;
	int16_t         root_x;
	int16_t         root_y;
	xcb_window_t    child;
	int16_t         event_x;
	int16_t         event_y;
};

// adding more states -- add NUM_STATES above ^^^
void state_idle(struct Context* ctx);
void state_window_move(struct Context* ctx);
void state_window_resize(struct Context* ctx);

extern struct Context ctx;
void setup_state_machine();
void destruct_state_machine();
void handle_state_event(xcb_generic_event_t* event);

const char* mod_key_name(enum ModKey mod_key);
const char* event_type_name(enum EventType event_type);

#endif
