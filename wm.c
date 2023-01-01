#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wm-log.h"
#include "wm-signals.h"
#include "wm-running.h"

int main(int argc, char** argv) {
	setup_signals();
	while (running) {
		usleep(100000); // 100ms
	}
	return 0;
}
