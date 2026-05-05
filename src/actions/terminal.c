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
#include <signal.h>
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
 * We store a copy since callers may free their buffer
 */
static char* custom_terminal = NULL;

/*
 * Default fallback terminal
 */
#define DEFAULT_TERMINAL     "xterm"

/*
 * Maximum terminal command length
 */
#define TERMINAL_MAX_CMD_LEN 256

/*
 * Get the terminal command to execute.
 * Returns the custom command if set, otherwise $TERMINAL env var,
 * or the fallback default.
 *
 * @return  terminal command string (owned by this module, do not free)
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
 * The command is copied internally so callers can free their buffer.
 */
void
terminal_set_command(const char* cmd)
{
  /* Free existing custom command if any */
  if (custom_terminal != NULL) {
    free(custom_terminal);
    custom_terminal = NULL;
  }

  if (cmd == NULL || cmd[0] == '\0') {
    return;
  }

  /* Copy the command string */
  size_t len = strlen(cmd);
  if (len >= TERMINAL_MAX_CMD_LEN) {
    LOG_WARN("Terminal command too long (max %d chars)", TERMINAL_MAX_CMD_LEN - 1);
    len = TERMINAL_MAX_CMD_LEN - 1;
  }

  custom_terminal = malloc(len + 1);
  if (custom_terminal != NULL) {
    memcpy(custom_terminal, cmd, len);
    custom_terminal[len] = '\0';
  } else {
    LOG_ERROR("Failed to allocate terminal command string");
  }
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

  /*
   * Note: We intentionally do NOT install a SIGCHLD handler.
   *
   * Rationale: The launcher module (launcher.c) needs to wait for its forked
   * dmenu child using waitpid(pid, ...). A process-wide SIGCHLD handler that
   * reaps all children (waitpid(-1, ...)) would steal children from the
   * launcher, causing waitpid to fail with ECHILD.
   *
   * Terminal children are reaped using WNOHANG after spawn() returns.
   * For long-running terminal processes, they will eventually become zombies,
   * but since terminals are typically long-lived, this is acceptable.
   * Alternatively, the window manager process could be configured to ignore
   * SIGCHLD signals from child processes using prctl(PR_SET_PDEATHSIG).
   */

  initialized = true;
  LOG_DEBUG("Terminal module initialized (using: %s)", terminal_get_default());
}

/*
 * Shutdown the terminal module.
 * Unregisters the action and frees custom terminal string.
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

  /* Free custom terminal string */
  if (custom_terminal != NULL) {
    free(custom_terminal);
    custom_terminal = NULL;
  }

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
 * Uses shell to parse command, supporting arguments and flags.
 *
 * Note: This is a fire-and-forget operation. The child process is not
 * explicitly reaped here - the kernel will deliver SIGCHLD when it exits.
 * For long-running terminal emulators, this is not a problem as they
 * typically outlive the window manager session.
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
     * Use setsid to create a new session and detach from controlling tty.
     * This prevents the terminal from receiving signals from the WM's tty.
     */
    if (setsid() < 0) {
      LOG_DEBUG("Child: setsid failed (non-fatal): %s", strerror(errno));
      /* Continue anyway - the exec will likely work */
    }

    const char* term_cmd = terminal_get_default();

    /*
     * Use sh -c to parse the command, allowing:
     * - Commands with arguments: "alacritty -e tmux"
     * - Commands with flags: "kitty --hold"
     * - Shell built-ins and complex commands
     *
     * This is safe because the command comes from user configuration
     * ($TERMINAL env var or custom command set by user).
     */
    execlp("sh", "sh", "-c", term_cmd, (char*) NULL);

    /* If execlp returns, it failed */
    LOG_DEBUG("Child: failed to execute terminal '%s': %s", term_cmd, strerror(errno));
    _exit(1);
  }

  /* Parent process - try to reap immediately */
  /* Use WNOHANG to avoid blocking - child may still be running */
  int status;
  pid_t reaped = waitpid(pid, &status, WNOHANG);

  if (reaped > 0) {
    /* Child exited immediately (likely exec failed) */
    LOG_DEBUG("Terminal child exited immediately: pid=%d status=%d",
              (int) pid, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  } else {
    /* Child is still running (normal case) */
    LOG_DEBUG("Terminal spawned: pid=%d cmd=%s", (int) pid, terminal_get_default());
  }

  return true;
}