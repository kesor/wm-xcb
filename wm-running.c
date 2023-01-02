#include <signal.h>
#include "wm-running.h"

/* initialize the running flag */
sig_atomic_t running = 1;
