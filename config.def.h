#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <xcb/xcb.h>
#include "wm-states.h"

static struct StateTransition transitions[] = {
	/* hover window type , combination of buttons and keys , event type , state to move from , state to move into */
	{ WndTypeClient, Btn1 | KeyMod1, OnPress, STATE_IDLE, STATE_WINDOW_MOVE },
	{ WndTypeClient, Btn1, OnRelease, STATE_WINDOW_MOVE, STATE_IDLE },

	{ WndTypeClient, Btn3 | KeyMod1, OnPress, STATE_IDLE, STATE_WINDOW_RESIZE },
	{ WndTypeClient, Btn3, OnRelease, STATE_WINDOW_RESIZE, STATE_IDLE },
};

#endif
