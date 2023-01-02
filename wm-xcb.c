#include <xcb/xcb.h>
#include <xcb/errors.h>

#include "wm-running.h"
#include "wm-log.h"
#include "wm-xcb.h"
#include "wm-xcb-events.h"

xcb_connection_t* dpy;
xcb_window_t root;

void check_request(
	xcb_connection_t* dpy,
	xcb_void_cookie_t cookie,
	const char* request_name
) {
	xcb_generic_error_t* error = xcb_request_check(dpy, cookie);
	if (error) {
		LOG_ERROR("Error executing '%s' request: %d", request_name, error->error_code);
		xcb_errors_context_t* err_ctx;
		xcb_errors_context_new(dpy, &err_ctx);
		const char* major, * minor, * extension, * error_name;
		major = xcb_errors_get_name_for_major_code(err_ctx, error->major_code);
		minor = xcb_errors_get_name_for_minor_code(err_ctx, error->major_code, error->minor_code);
		error_name = xcb_errors_get_name_for_error(err_ctx, error->error_code, &extension);
		LOG_ERROR("XCB: %s:%s, %s:%s, resource %u sequence %u",
			error_name, extension ? extension : "no_extension",
			major, minor ? minor : "no_minor",
			(unsigned int)error->resource_id,
			(unsigned int)error->sequence);
		xcb_errors_context_free(err_ctx);
		free(error);
		return;
	}
	LOG_DEBUG("xcb request check for '%s' returned no error for cookie %d", request_name, cookie.sequence);
}

static void connect_to_x_display(xcb_connection_t** dpy) {
	char* displayname = NULL;
	char* hostname = NULL;
	int display_number = 0;
	int screen_number = 0;

	if (xcb_parse_display(displayname, &hostname, &display_number, &screen_number) == 0)
		LOG_FATAL("dwm: failed to parse display name");

	/* connecting to remote hosts not really supported here with a simple xcb_connect() */
	free(hostname);

	*dpy = xcb_connect(displayname, &screen_number);
	if (xcb_connection_has_error(*dpy))
		LOG_FATAL("dwm: failed to open display");
}

void get_root_window(xcb_connection_t* dpy, xcb_window_t* root) {
	const xcb_setup_t* setup = xcb_get_setup(dpy);
	xcb_screen_t* screen = xcb_setup_roots_iterator(setup).data;
	*root = screen->root;
}

void setup_xcb() {
	static uint32_t values[3];

	connect_to_x_display(&dpy);
	get_root_window(dpy, &root);

	values[0] = XCB_EVENT_MASK_KEY_PRESS
		| XCB_EVENT_MASK_KEY_RELEASE
		| XCB_EVENT_MASK_BUTTON_PRESS
		| XCB_EVENT_MASK_BUTTON_RELEASE
		| XCB_EVENT_MASK_ENTER_WINDOW
		| XCB_EVENT_MASK_LEAVE_WINDOW
		| XCB_EVENT_MASK_POINTER_MOTION
		| XCB_EVENT_MASK_POINTER_MOTION_HINT
		| XCB_EVENT_MASK_BUTTON_1_MOTION
		| XCB_EVENT_MASK_BUTTON_2_MOTION
		| XCB_EVENT_MASK_BUTTON_3_MOTION
		| XCB_EVENT_MASK_BUTTON_4_MOTION
		| XCB_EVENT_MASK_BUTTON_5_MOTION
		| XCB_EVENT_MASK_BUTTON_MOTION
		| XCB_EVENT_MASK_KEYMAP_STATE
		| XCB_EVENT_MASK_EXPOSURE
		| XCB_EVENT_MASK_VISIBILITY_CHANGE
		| XCB_EVENT_MASK_STRUCTURE_NOTIFY
		| XCB_EVENT_MASK_RESIZE_REDIRECT
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
		| XCB_EVENT_MASK_FOCUS_CHANGE
		| XCB_EVENT_MASK_PROPERTY_CHANGE
		| XCB_EVENT_MASK_COLOR_MAP_CHANGE
		| XCB_EVENT_MASK_OWNER_GRAB_BUTTON
		;

	const static uint32_t mask = XCB_CW_EVENT_MASK;
	xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(dpy, root, mask, values);
	check_request(dpy, cookie, "root window attributes change");

	if (xcb_flush(dpy) <= 0)
		LOG_FATAL("failed to flush.");

	LOG_DEBUG("root window id: %d", root);
}

void destruct_xcb() {
	xcb_disconnect(dpy);
}

void handle_xcb_events() {
	// -- non-blocking event waiting
	xcb_generic_event_t* event = xcb_poll_for_event(dpy);
	if (event == NULL)
		return;

	// -- blocking way for waiting for events
	// LOG_DEBUG("waiting for xcb events, running is: %d", running);
	// xcb_generic_event_t* event = xcb_wait_for_event(dpy);
	// LOG_DEBUG("received an event! %d", (int)event->response_type);
	// if (event == NULL)
	// 	LOG_FATAL("error waiting for event");

	switch (event->response_type) {
	case 0: {
		xcb_generic_error_t* err = (xcb_generic_error_t*)event;
		xcb_errors_context_t* err_ctx;
		xcb_errors_context_new(dpy, &err_ctx);
		const char* major, * minor, * extension, * error;
		major = xcb_errors_get_name_for_major_code(err_ctx, err->major_code);
		minor = xcb_errors_get_name_for_minor_code(err_ctx, err->major_code, err->minor_code);
		error = xcb_errors_get_name_for_error(err_ctx, err->error_code, &extension);
		LOG_DEBUG("XCB Error: %s:%s, %s:%s, resource %u sequence %u\n",
			error, extension ? extension : "no_extension",
			major, minor ? minor : "no_minor",
			(unsigned int)err->resource_id,
			(unsigned int)err->sequence);
		xcb_errors_context_free(err_ctx);
		break;
	}

	case XCB_KEY_PRESS: handle_key_press_release((xcb_key_press_event_t*)event); break;
	case XCB_KEY_RELEASE: handle_key_press_release((xcb_key_release_event_t*)event); break;
	case XCB_BUTTON_PRESS: handle_button_press_release((xcb_button_press_event_t*)event); break;
	case XCB_BUTTON_RELEASE: handle_button_press_release((xcb_button_release_event_t*)event); break;
	case XCB_MOTION_NOTIFY: LOG_DEBUG("event: motion notify"); break;
	case XCB_ENTER_NOTIFY: handle_enter_notify((xcb_enter_notify_event_t*)event); break;
	case XCB_LEAVE_NOTIFY: handle_leave_notify((xcb_leave_notify_event_t*)event); break;
	case XCB_FOCUS_IN: LOG_DEBUG("event: focus in"); break;
	case XCB_FOCUS_OUT: LOG_DEBUG("event: focus out"); break;
	case XCB_KEYMAP_NOTIFY: LOG_DEBUG("event: keymap notify"); break;
	case XCB_EXPOSE: LOG_DEBUG("event: expose"); break;
	case XCB_GRAPHICS_EXPOSURE: LOG_DEBUG("event: graphics exposure"); break;
	case XCB_NO_EXPOSURE: LOG_DEBUG("event: no exposure"); break;
	case XCB_VISIBILITY_NOTIFY: LOG_DEBUG("event: visibility notify"); break;
	case XCB_CREATE_NOTIFY: handle_create_notify((xcb_create_notify_event_t*)event); break;
	case XCB_DESTROY_NOTIFY: handle_destroy_notify((xcb_destroy_notify_event_t*)event); break;
	case XCB_UNMAP_NOTIFY: LOG_DEBUG("event: unmap notify"); break;
	case XCB_MAP_NOTIFY: LOG_DEBUG("event: map notify"); break;
	case XCB_MAP_REQUEST: handle_map_request((xcb_map_request_event_t*)event); break;
	case XCB_REPARENT_NOTIFY: LOG_DEBUG("event: reparent notify"); break;
	case XCB_CONFIGURE_NOTIFY: LOG_DEBUG("event: configure notify"); break;
	case XCB_CONFIGURE_REQUEST: LOG_DEBUG("event: configure request"); break;
	case XCB_GRAVITY_NOTIFY: LOG_DEBUG("event: gravity notify"); break;
	case XCB_RESIZE_REQUEST: LOG_DEBUG("event: resize request"); break;
	case XCB_CIRCULATE_NOTIFY: LOG_DEBUG("event: circulate notify"); break;
	case XCB_CIRCULATE_REQUEST: LOG_DEBUG("event: circulate request"); break;
	case XCB_PROPERTY_NOTIFY: handle_property_notify((xcb_property_notify_event_t*)event); break;
	case XCB_SELECTION_CLEAR: LOG_DEBUG("event: selection clear"); break;
	case XCB_SELECTION_REQUEST: LOG_DEBUG("event: selection request"); break;
	case XCB_SELECTION_NOTIFY: LOG_DEBUG("event: selection notify"); break;
	case XCB_COLORMAP_NOTIFY: LOG_DEBUG("event: colormap notify"); break;
	case XCB_CLIENT_MESSAGE: LOG_DEBUG("event: client message"); break;
	case XCB_MAPPING_NOTIFY: LOG_DEBUG("event: mapping notify"); break;
	case XCB_GE_GENERIC: LOG_DEBUG("event: ge generic"); break;
	case XCB_REQUEST: LOG_DEBUG("event: xcb request"); break;

	default:
		printf("Event: XCB Unknown Event\n");
	}

	free(event);
}