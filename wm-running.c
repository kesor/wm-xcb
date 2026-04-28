#include "wm-running.h"
#include <signal.h>

/* initialize the running flag */
sig_atomic_t running = 1;
