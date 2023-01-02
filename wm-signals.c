#include <stdbool.h>
#include <signal.h>
#include <sys/wait.h>
#include <xcb/xcb.h>

#include "wm-log.h"
#include "wm-signals.h"
#include "wm-running.h"
#include "wm-xcb.h"

void sigchld(int unused) {
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		LOG_ERROR("cannot install SIGCHLD event handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

void sigint(int unused) {
	running = 0;

	printf("\n");
	LOG_DEBUG("received interrupt signal.");

	/* when XCB is blocking-waitfor events, changing running to 0 doesn't exit. */
	// xcb_generic_event_t event;
	// event.response_type = XCB_CLIENT_MESSAGE;
	// xcb_send_event(dpy, false, root, XCB_EVENT_MASK_NO_EVENT, (char*)&event);
	// xcb_flush(dpy);
}

void setup_signals() {
	if (signal(SIGINT, sigint) == SIG_ERR)
		LOG_FATAL("cannot install SIGINT event handler");

	if (signal(SIGTERM, sigint) == SIG_ERR)
		LOG_FATAL("cannot install SIGTERM event handler");
}
