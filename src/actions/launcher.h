/*
 * launcher.h - Application launcher action using dmenu
 *
 * This module provides the "app.launch" action that invokes dmenu
 * for application launching. It forks a dmenu process and, when the event loop
 * reports the selection, executes it in another process.
 *
 * Design:
 * - Action: "app.launch" (no target required)
 * - Forks dmenu, returns without waiting for the selection
 * - Reads the selection from a pipe registered with the event loop
 * - Executes the selected command via fork+exec
 * - No XCB keyboard grab: dmenu grabs the keyboard itself
 */

#ifndef _WM_LAUNCHER_H_
#define _WM_LAUNCHER_H_

#include <stdbool.h>
#include <stdint.h>

#include "action-registry.h"
#include "src/event-types.h"

/*
 * Maximum command length for dmenu output
 */
#define LAUNCHER_MAX_CMD_LENGTH 256

/*
 * Launcher events on the hub.
 *
 * EVT_LAUNCHER_FAILED: the prompt closed without a selection, or the launcher
 * could not start at all. The first is fired from the loop, when the pipe
 * reaches its end; the second from the action, because it is known before the
 * action returns. A second launch while a prompt is open is neither: it is
 * refused without an event, since the open prompt still owes its own answer.
 */
enum LauncherEvents {
  EVT_LAUNCHER_FAILED = EVT_LAUNCHER_BASE + 0,
};

/*
 * Initialize the launcher module.
 * Registers the "app.launch" action with the action registry.
 */
void launcher_init(void);

/*
 * Shutdown the launcher module.
 * Unregisters the action and stops watching an open dmenu.
 */
void launcher_shutdown(void);

/*
 * Check if the module is initialized (for the startup check)
 */
bool launcher_is_initialized(void);

/*
 * Whether a dmenu is open and its selection is still awaited.
 */
bool launcher_is_running(void);

/*
 * Action callback for launching applications via dmenu.
 * Starts dmenu and returns; the command runs when the selection arrives.
 *
 * @param inv  Action invocation (target not used for this action)
 * @return     true when dmenu was started, false when it could not be started
 *             or a prompt is already open
 */
bool launcher_action_launch(ActionInvocation* inv);

/*
 * Start dmenu and execute the selected command when it arrives.
 * Returns once dmenu is running, not when it exits: the pipe is registered with
 * the event loop, which reads it.
 *
 * @return  true when dmenu was started, false otherwise. This function emits
 *          no event for any failure: the action reports a start failure, and a
 *          prompt that is already open is a refusal rather than a failure.
 */
bool launcher_run_dmenu(void);

/*
 * Default dmenu command arguments.
 * Can be overridden via config if needed.
 */
typedef struct LauncherDmenuConfig {
  const char* prompt;    /* Prompt text (e.g., "Run: ") */
  const char* font;      /* Font to use */
  const char* color;     /* Normal color */
  const char* sel_color; /* Selected color */
  int         x_offset;  /* X position offset */
  int         y_offset;  /* Y position offset */
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
