// enum State {
// 	STATE_IDLE,
// 	STATE_MOUSE_MOVE,
// 	STATE_MOUSE_RESIZE,
// 	NUM_STATES
// };

// struct StateTransition {
// 	enum WindowType hover_target;
// 	int mouse_button_state;
// 	int keyboard_key_state;
// 	enum State new_state;
// };

// static StateTransition transitions[] = {
// 	{ ClkTagBar,     Button1,      0, STATE_TAG_SELECT },
// 	{ ClkTagBar,     Button3,      0, STATE_TAG_TOGGLE },

// 	{ ClkWinTitle,   Button2,      0, STATE_WINDOW_ZOOM },

// 	{ ClkStatusText, Button2,      0, STATE_SPAWN_TERMINAL },

// 	{ ClkClientWin,  Button1,      0, STATE_MOUSE_MOVE },
// 	{ ClkClientWin,  Button2,      0, STATE_TOGGLE_FLOATING },
// 	{ ClkClientWin,  Button3,      0, STATE_MOUSE_RESIZE },

// 	{ ClkTagBar,     Button1,      0, STATE_VIEW_TAG },
// 	{ ClkTagBar,     Button3,      0, STATE_TOGGLE_VIEW },
// 	{ ClkTagBar,     Button1, MODKEY, STATE_TAG_SELECT },
// 	{ ClkTagBar,     Button3, MODKEY, STATE_TAG_TOGGLE },
// };

// enum EventType {
// 	KEY_PRESSED,
// 	MOUSE_BUTTON_PRESSED,
// 	HOVER,
// 	// other event types
// };

// struct StateTransition {
// 	enum EventType event_type;
// 	union {
// 		unsigned int key;
// 		unsigned int mouse_button;
// 		unsigned int hover_target;
// 	};
// 	enum State new_state;
// };

// // example state transitions
// static const struct StateTransition transitions[] = {
// 	{ KEY_PRESSED, .key = XK_p, .new_state = STATE_LAUNCH_DMENU },
// 	{ KEY_PRESSED, .key = XK_b, .new_state = STATE_TOGGLE_BAR },
// 	{ MOUSE_BUTTON_PRESSED, .mouse_button = Button1, .new_state = STATE_MOUSE_MOVE },
// 	{ MOUSE_BUTTON_PRESSED, .mouse_button = Button2, .new_state = STATE_TOGGLE_FLOATING },
// 	{ MOUSE_BUTTON_PRESSED, .mouse_button = Button3, .new_state = STATE_MOUSE_RESIZE },
// 	{ HOVER, .hover_target = ClkTagBar, .new_state = STATE_VIEW },
// 	{ HOVER, .hover_target = ClkWinTitle, .new_state = STATE_ZOOM },
// 	// other state transitions
// };
