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
 * SIGCHLD handler to reap zombie processes.
 * This ensures forked terminal processes don't become zombies.
 */
static void
sigchld_handler(int sig)
{
  (void) sig;

  /* Reap all terminated children */
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
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

  /* Install SIGCHLD handler to reap zombie processes */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags   = SA_RESTART;

  if (sigaction(SIGCHLD, &sa, NULL) < 0) {
    LOG_WARN("Failed to install SIGCHLD handler: %s", strerror(errno));
    /* Non-fatal - zombies may occur but terminal will still work */
  }

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

  /* Parent process - log and return immediately */

  LOG_DEBUG("Terminal spawned: pid=%d cmd=%s", (int) pid, terminal_get_default());

  /*
   * We don't wait for the child - this is fire-and-forget.
   * The SIGCHLD handler will reap it when it exits.
   */

  return true;
}