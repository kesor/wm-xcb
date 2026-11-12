/*
 * terminal.h - Terminal spawning action
 *
 * Provides the "terminal.spawn" action for launching a terminal emulator.
 * This is a system-wide action that doesn't require a target.
 *
 * Design:
 * - Action: "terminal.spawn" with ACTION_TARGET_NONE (no target needed)
 * - Forks a child process
 * - Runs the first of $TERMINAL, x-terminal-emulator and a short list of
 *   terminals that is installed
 * - Fire-and-forget (returns immediately)
 */

#ifndef _WM_TERMINAL_H_
#define _WM_TERMINAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "src/actions/action-registry.h"

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
 * Check if the module is initialized (for the startup check)
 */
bool terminal_is_initialized(void);

/*
 * Action callback for spawning a terminal.
 *
 * This function:
 * 1. Resolves an installed terminal command, reporting what was tried when
 *    there is none
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
 * @return  true if fork succeeded, false on error or when no terminal
 *          emulator is installed
 */
bool terminal_spawn(void);

/*
 * Get the terminal command to execute: a command set with
 * terminal_set_command(), otherwise the first of $TERMINAL, x-terminal-emulator
 * and the compiled list of terminals that is installed on $PATH.
 *
 * @return  terminal command string (owned by this module, do not free), or
 *          NULL when no terminal emulator is installed
 */
const char* terminal_get_default(void);

/*
 * One line naming what a machine with no terminal emulator was looked for:
 * $TERMINAL and every compiled candidate. Logged at startup and when a press
 * asks for a terminal that is not there.
 *
 * @return  the report, owned by this module and valid until the next call
 */
const char* terminal_missing_report(void);

/*
 * Set a custom terminal command.
 * If set, this command will be used instead of $TERMINAL env var.
 *
 * @param cmd  terminal command to use (is copied internally)
 */
void terminal_set_command(const char* cmd);

#endif /* _WM_TERMINAL_H_ */