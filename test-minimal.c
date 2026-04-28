#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Minimal stub for wm-log.h */
#define LOG_DEBUG(pFormat, ...) { fprintf(stderr, "DEBUG: " pFormat "\n", ##__VA_ARGS__); }
#define LOG_ERROR(pFormat, ...) { fprintf(stderr, "ERROR: " pFormat "\n", ##__VA_ARGS__); }
#define LOG_CLEAN(pFormat, ...) { printf(pFormat "\n", ##__VA_ARGS__); }

/* Minimal stub for wm-running.h */
int running = 1;

#include "wm-hub.h"

int main(void) {
    hub_init();
    printf("Components: %d, Targets: %d\n", hub_component_count(), hub_target_count());
    hub_shutdown();
    printf("After shutdown - Components: %d, Targets: %d\n", hub_component_count(), hub_target_count());
    return 0;
}
