#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wm-log.h"
#include "wm-signals.h"
#include "wm-running.h"

#include "wm-xcb.h"
#include "wm-xcb-ewmh.h"

int main(int argc, char** argv) {
	setup_signals();
	setup_xcb();
	setup_ewmh();
	while (running) {
		handle_xcb_events();
		// usleep(100000); // 100ms
		usleep(100); // 100ns
	}
	destruct_ewmh();
	destruct_xcb();
	return 0;
}
