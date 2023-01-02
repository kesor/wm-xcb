#ifndef _WM_LOG_
#define _WM_LOG_

#include <stdio.h>
#include <stdlib.h>

#include "wm-running.h"

void logger(FILE* io, const char* fmt, ...);

#ifdef DEBUG
#	define LOG_DEBUG(pFormat, ...) { logger(stdout, "DEBUG: "pFormat, ##__VA_ARGS__); }
#else
#	define LOG_DEBUG(pFormat, ...) {}
#endif

#define LOG_ERROR(pFormat, ...) { logger(stderr, "ERROR: "pFormat, ##__VA_ARGS__); }
#define LOG_FATAL(pFormat, ...) { logger(stderr, "FATAL: "pFormat, ##__VA_ARGS__); running = 0; }

#endif
