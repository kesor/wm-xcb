/*
 * terminal.h - Terminal spawning action
 *
 * Provides the "terminal.spawn" action for launching a terminal emulator.
 * This is a system-wide action that doesn't require a target.
 *
 * Design:
 * - Action: "terminal.spawn" with ACTION_TARGET_NONE (no target needed)
 * - Forks a child process
 * - Executes $TERMINAL env var or falls back to "xterm"
 * - Fire-and-forget (returns immediately)
 */

#ifndef _WM_TERMINAL_H_
#define _WM_TERMINAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "action-registry.h"

/*
 * Initialize the terminal module.
 * Registers the "terminal.spawn" action with the action registry.
 */
void terminal_init(void);

/*
 * Shutdown the terminal module.
 * Unregisters the action.
 */
void terminal_shutdown(void);

/*
 * Action callback for spawning a terminal.
 *
 * This function:
 * 1. Gets terminal command from $TERMINAL env var or default
 * 2. Forks a child process
 * 3. Child executes the terminal command
 * 4. Parent returns immediately (fire-and-forget)
 *
 * @param inv  Action invocation (target not used for this action)
 * @return     true on successful fork, false on error
 */
bool terminal_action_spawn(ActionInvocation* inv);

/*
 * Spawn a terminal in a new process.
 * Uses fork()+exec() to avoid affecting the window manager.
 *
 * @return  true if fork succeeded, false on error
 */
bool terminal_spawn(void);

/*
 * Get the default terminal command.
 * Returns the value of $TERMINAL env var or fallback default.
 *
 * @return  terminal command string (owned by this module, do not free)
 */
const char* terminal_get_default(void);

/*
 * Set a custom terminal command.
 * If set, this command will be used instead of $TERMINAL env var.
 *
 * @param cmd  terminal command to use (is copied internally)
 */
void terminal_set_command(const char* cmd);

#endif /* _WM_TERMINAL_H_ */