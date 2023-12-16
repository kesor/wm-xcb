#include <xcb/xcb.h>

void print_modifiers(uint32_t mask) {
	const char* MODIFIERS[] = {
		"Shift", "Lock", "Ctrl", "Alt",
		"Mod2", "Mod3", "Mod4", "Mod5",
		"Button1", "Button2", "Button3", "Button4", "Button5"
	};

	for (const char** modifier = MODIFIERS; mask; mask >>= 1, ++modifier)
		if (mask & 1)
			LOG_DEBUG("modifier in mask: %s", *modifier);
}

void
movemouse(const Arg* arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client* c;
	Monitor* m;
	xcb_generic_event_t* ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	xcb_grab_pointer_cookie_t grab_cookie = xcb_grab_pointer(
		dpy, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, cursor[CurMove]->cursor, XCB_CURRENT_TIME);
	xcb_grab_pointer_reply_t* grab_reply = xcb_grab_pointer_reply(dpy, grab_cookie, NULL);
	if (grab_reply->status != XCB_GRAB_STATUS_SUCCESS) {
		free(grab_reply);
		return;
	}
	free(grab_reply);
	if (!getrootptr(&x, &y))
		return;
	do {
		ev = xcb_wait_for_event(dpy);
		switch (ev->response_type & ~0x80) {
		case XCB_CONFIGURE_REQUEST:
		case XCB_EXPOSE:
		case XCB_MAP_REQUEST:
			handler[ev->response_type & ~0x80](ev);
			break;
		case XCB_MOTION_NOTIFY: {
			xcb_motion_notify_event_t* motion = (xcb_motion_notify_event_t*)ev;
			if ((motion->time - lasttime) <= (1000 / 60))
				continue;
			lasttime = motion->time;

			nx = ocx + (motion->event_x - x);
			ny = ocy + (motion->event_y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HE
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
					&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
					togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
		default:
			break;
		}
		free(ev);
	} while (ev->response_type != XCB_BUTTON_RELEASE);
	xcb_ungrab_pointer(dpy, XCB_CURRENT_TIME);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}
