#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <xcb/xcb.h>
#include "wm-states.h"

#define XK_LATIN1
#include <X11/keysymdef.h>

static struct StateTransition transitions[] = {
	/* hover window type , combination of buttons and keys , event type , state to move from , state to move into */
	{ WndTypeClient, EventTypeBtnPress,   KeyShift, 1,    0, STATE_IDLE, STATE_WINDOW_MOVE },
	{ WndTypeClient, EventTypeBtnRelease,  KeyNone, 1,    0, STATE_WINDOW_MOVE, STATE_IDLE },

	{ WndTypeClient, EventTypeBtnPress,   KeyShift, 3,    0, STATE_IDLE, STATE_WINDOW_RESIZE },
	{ WndTypeClient, EventTypeBtnRelease,        0, 3,    0, STATE_WINDOW_RESIZE, STATE_IDLE },

	{ WndTypeRoot,   EventTypeBtnPress,    KeyMod1, 1,    0, STATE_IDLE, STATE_WINDOW_MOVE },
	{ WndTypeRoot,   EventTypeKeyPress,   KeyShift, 0, XK_A, STATE_IDLE, STATE_IDLE },
};

#endif
