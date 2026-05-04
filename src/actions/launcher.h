/*
 * launcher.h - Application launcher action using dmenu
 *
 * This module provides the "app.launch" action that invokes dmenu
 * for application launching. It forks a dmenu process and executes
 * the user's selection.
 *
 * Design:
 * - Action: "app.launch" (no target required)
 * - Forks dmenu, waits for selection
 * - Executes selected command via fork+exec
 * - Uses XCB to grab keyboard during dmenu run
 */

#ifndef _WM_LAUNCHER_H_
#define _WM_LAUNCHER_H_

#include <stdbool.h>
#include <stdint.h>

#include "action-registry.h"

/*
 * Maximum command length for dmenu output
 */
#define LAUNCHER_MAX_CMD_LENGTH 256

/*
 * Initialize the launcher module.
 * Registers the "app.launch" action with the action registry.
 */
void launcher_init(void);

/*
 * Shutdown the launcher module.
 * Unregisters the action.
 */
void launcher_shutdown(void);

/*
 * Action callback for launching applications via dmenu.
 * Forks dmenu, reads selection, executes command.
 *
 * @param inv  Action invocation (target not used for this action)
 * @return     true on successful launch, false on failure
 */
bool launcher_action_launch(ActionInvocation* inv);

/*
 * Run dmenu and execute the selected command.
 * This is a synchronous operation - it blocks until dmenu exits.
 *
 * @return  true if a command was executed, false otherwise
 */
bool launcher_run_dmenu(void);

/*
 * Default dmenu command arguments.
 * Can be overridden via config if needed.
 */
typedef struct LauncherDmenuConfig {
  const char* prompt;        /* Prompt text (e.g., "Run: ") */
  const char* font;          /* Font to use */
  const char* color;         /* Normal color */
  const char* sel_color;     /* Selected color */
  int         x_offset;      /* X position offset */
  int         y_offset;      /* Y position offset */
  bool        grab_keyboard; /* Grab keyboard during dmenu run */
} LauncherDmenuConfig;

/*
 * Get the current dmenu configuration.
 */
const LauncherDmenuConfig* launcher_get_dmenu_config(void);

/*
 * Set the dmenu configuration.
 * Must be called before launcher_init().
 */
void launcher_set_dmenu_config(LauncherDmenuConfig config);

#endif /* _WM_LAUNCHER_H_ */