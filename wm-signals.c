#include <signal.h>
#include <sys/wait.h>

#include "wm-log.h"
#include "wm-signals.h"
#include "wm-running.h"

void sigchld(int unused) {
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		LOG_ERROR("cannot install SIGCHLD event handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

void sigint(int unused) {
	running = 0;
	printf("\n");
	LOG_DEBUG("received interrupt signal.");
}

void setup_signals() {
	if (signal(SIGINT, sigint) == SIG_ERR)
		LOG_FATAL("cannot install SIGINT event handler");
}
