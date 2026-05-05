/*
 * terminal.c - Terminal spawning implementation
 *
 * Implements the "terminal.spawn" action for launching a terminal emulator.
 * This is a system-wide action that doesn't require a target.
 *
 * Design:
 * - Action: "terminal.spawn" with ACTION_TARGET_NONE (no target needed)
 * - Forks a child process to execute the terminal
 * - Returns immediately (fire-and-forget)
 *
 * Security note: Command execution is intentional - this spawns the user's
 * configured terminal, not arbitrary commands.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "action-registry.h"
#include "terminal.h"
#include "wm-log.h"

/*
 * Action definition for terminal.spawn
 */
static Action terminal_action = {
  .name            = "terminal.spawn",
  .description     = "Spawn a new terminal emulator",
  .callback        = terminal_action_spawn,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_NONE,
  .target_required = false,
  .userdata        = NULL,
};

/*
 * Track if terminal module is initialized
 */
static bool initialized = false;

/*
 * Custom terminal command (NULL = use env var)
 */
static const char* custom_terminal = NULL;

/*
 * Default fallback terminal
 */
#define DEFAULT_TERMINAL "xterm"

/*
 * Maximum terminal command length
 */
#define TERMINAL_MAX_CMD_LEN 256

/*
 * Get the terminal command to execute.
 * Returns the custom command if set, otherwise $TERMINAL env var,
 * or the fallback default.
 *
 * @return  terminal command string (caller must not modify or free)
 */
const char*
terminal_get_default(void)
{
  /* Use custom command if set */
  if (custom_terminal != NULL && custom_terminal[0] != '\0') {
    return custom_terminal;
  }

  /* Try $TERMINAL environment variable */
  const char* env_term = getenv("TERMINAL");
  if (env_term != NULL && env_term[0] != '\0') {
    return env_term;
  }

  /* Fallback to default */
  return DEFAULT_TERMINAL;
}

/*
 * Set a custom terminal command.
 */
void
terminal_set_command(const char* cmd)
{
  custom_terminal = cmd;
}

/*
 * Initialize the terminal module.
 * Registers the "terminal.spawn" action with the action registry.
 */
void
terminal_init(void)
{
  if (initialized) {
    LOG_DEBUG("Terminal module already initialized");
    return;
  }

  LOG_DEBUG("Initializing terminal module");

  /* Register the action */
  if (!action_register(&terminal_action)) {
    LOG_ERROR("Failed to register terminal.spawn action");
    return;
  }

  initialized = true;
  LOG_DEBUG("Terminal module initialized (using: %s)", terminal_get_default());
}

/*
 * Shutdown the terminal module.
 * Unregisters the action.
 */
void
terminal_shutdown(void)
{
  if (!initialized) {
    return;
  }

  LOG_DEBUG("Shutting down terminal module");

  /* Unregister the action */
  action_unregister("terminal.spawn");

  initialized = false;
  LOG_DEBUG("Terminal module shutdown complete");
}

/*
 * Action callback for spawning a terminal.
 */
bool
terminal_action_spawn(ActionInvocation* inv)
{
  (void) inv;
  return terminal_spawn();
}

/*
 * Spawn a terminal in a new process.
 * Uses fork()+exec() to avoid affecting the window manager.
 */
bool
terminal_spawn(void)
{
  pid_t pid = fork();

  if (pid < 0) {
    LOG_ERROR("Failed to fork for terminal: %s", strerror(errno));
    return false;
  }

  if (pid == 0) {
    /* Child process - execute terminal */

    /*
     * Reset signal handlers to default (we're a child process)
     */
    signal(SIGCHLD, SIG_DFL);

    /*
     * Use setsid to create a new session and detach from controlling tty.
     * This prevents the terminal from receiving signals from the WM's tty.
     */
    if (setsid() < 0) {
      LOG_DEBUG("Child: setsid failed (non-fatal): %s", strerror(errno));
      /* Continue anyway - the exec will likely work */
    }

    const char* term_cmd = terminal_get_default();

    /*
     * Execute the terminal.
     * execlp searches PATH for the command.
     */
    execlp(term_cmd, term_cmd, (char*) NULL);

    /* If execlp returns, it failed */
    LOG_DEBUG("Child: failed to execute terminal '%s': %s", term_cmd, strerror(errno));
    _exit(1);
  }

  /* Parent process - log and return immediately */

  LOG_DEBUG("Terminal spawned: pid=%d cmd=%s", (int) pid, terminal_get_default());

  /*
   * We don't wait for the child - this is fire-and-forget.
   * The child will become orphaned and continue running independently.
   */

  return true;
}