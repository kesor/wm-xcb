/*
 * terminal.c - Terminal spawning implementation
 *
 * Implements the "terminal.spawn" action for launching a terminal emulator.
 * This is a system-wide action that doesn't require a target.
 *
 * Design:
 * - Action: "terminal.spawn" with ACTION_TARGET_NONE (no target needed)
 * - Picks a terminal that is installed: $TERMINAL, x-terminal-emulator, then
 *   the names in terminal_candidates
 * - Forks a child process to execute the terminal
 * - Returns immediately (fire-and-forget)
 *
 * Security note: Command execution is intentional - this spawns the user's
 * configured terminal, not arbitrary commands.
 */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "action-registry.h"
#include "src/core/wm-log.h"
#include "src/core/wm-xcb.h"
#include "terminal.h"

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
 * Maximum terminal command length
 */
#define TERMINAL_MAX_CMD_LEN 256

/*
 * Looked up on $PATH in this order when the user has no usable $TERMINAL.
 *
 * x-terminal-emulator goes first because the distribution already points it at
 * the terminal this machine is set up for; a user who installed kitty and kept
 * xterm would otherwise get xterm. xterm goes last for that reason, not because
 * it is missing anywhere.
 */
static const char* const terminal_candidates[] = {
  "x-terminal-emulator",
  "alacritty",
  "kitty",
  "foot",
  "urxvt",
  "st",
  "xterm",
};

/*
 * Length of the program word in a command line: everything before the first
 * blank, so "$TERMINAL=alacritty -e tmux" is looked up as alacritty.
 */
static size_t
terminal_program_length(const char* cmd)
{
  size_t len = 0;

  while (cmd[len] != '\0' && cmd[len] != ' ' && cmd[len] != '\t')
    len++;

  return len;
}

static bool
terminal_is_executable(const char* path)
{
  struct stat st;

  return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static bool
terminal_in_directory(const char* dir, size_t dir_len, const char* program)
{
  char path[PATH_MAX];
  int  written;

  if (dir_len == 0) /* an empty PATH component is the current directory */
    written = snprintf(path, sizeof(path), "%s", program);
  else
    written = snprintf(path, sizeof(path), "%.*s/%s", (int) dir_len, dir, program);

  return written > 0 && (size_t) written < sizeof(path) && terminal_is_executable(path);
}

/*
 * Whether the program a command line starts with can be run. A command that
 * names a directory is checked where it stands; a bare name is searched on
 * $PATH, which is the same lookup execlp() does in the child.
 */
static bool
terminal_installed(const char* cmd)
{
  char   program[TERMINAL_MAX_CMD_LEN];
  size_t len = terminal_program_length(cmd);

  if (len == 0 || len >= sizeof(program))
    return false;

  memcpy(program, cmd, len);
  program[len] = '\0';

  if (strchr(program, '/') != NULL)
    return terminal_is_executable(program);

  const char* path = getenv("PATH");
  if (path == NULL)
    return false;

  for (;;) {
    const char* colon   = strchr(path, ':');
    size_t      dir_len = colon != NULL ? (size_t) (colon - path) : strlen(path);

    if (terminal_in_directory(path, dir_len, program))
      return true;
    if (colon == NULL)
      return false;
    path = colon + 1;
  }
}

/*
 * Get the terminal command to execute: the custom command if one was set,
 * otherwise the first of $TERMINAL and terminal_candidates that is installed.
 *
 * A $TERMINAL that names a program this machine does not have falls through
 * instead of being returned, because a key bound to a missing binary does
 * nothing at all and says nothing about why.
 *
 * @return  terminal command string (owned by this module, do not free), or
 *          NULL when no terminal emulator is installed
 */
const char*
terminal_get_default(void)
{
  if (custom_terminal != NULL && custom_terminal[0] != '\0')
    return custom_terminal;

  const char* env_term = getenv("TERMINAL");
  if (env_term != NULL && env_term[0] != '\0') {
    if (terminal_installed(env_term))
      return env_term;

    LOG_DEBUG("$TERMINAL names '%s', which is not installed; looking for another terminal", env_term);
  }

  for (size_t i = 0; i < sizeof(terminal_candidates) / sizeof(terminal_candidates[0]); i++) {
    if (terminal_installed(terminal_candidates[i]))
      return terminal_candidates[i];
  }

  return NULL;
}

/*
 * Append to a bounded buffer, truncating rather than writing past its end.
 */
static void
terminal_append(char* buf, size_t size, size_t* used, const char* text)
{
  if (*used >= size - 1)
    return;

  size_t room = size - 1 - *used;
  size_t len  = strlen(text);

  if (len > room)
    len = room;

  memcpy(buf + *used, text, len);
  *used += len;
  buf[*used] = '\0';
}

/*
 * One line naming every program that was tried, so the two places that notice
 * a machine with no terminal emulator say the same thing: startup, and the key
 * press that needs one.
 *
 * @return  the report, owned by this module and valid until the next call
 */
const char*
terminal_missing_report(void)
{
  static char report[512];
  size_t      used = 0;

  report[0] = '\0';
  terminal_append(report, sizeof(report), &used, "terminal.spawn: no terminal emulator installed; tried $TERMINAL");

  for (size_t i = 0; i < sizeof(terminal_candidates) / sizeof(terminal_candidates[0]); i++) {
    terminal_append(report, sizeof(report), &used, ", ");
    terminal_append(report, sizeof(report), &used, terminal_candidates[i]);
  }

  return report;
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
  /* Same as the launcher: keybinding_shutdown() clears the action registry
   * without calling terminal_shutdown(), so the flag alone is not proof that
   * terminal.spawn is still registered. */
  if (initialized && action_exists(terminal_action.name)) {
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
   * Children are reaped by the SIGCHLD handler that setup_signals() installs,
   * so this module does not wait for its own child. A terminal is typically
   * long-lived anyway, and waiting for it is exactly what the event loop must
   * not do.
   */

  initialized = true;

  /* Resolve once here so a machine with no terminal emulator says so at
   * startup, before the key is pressed, instead of only when the key does
   * nothing. */
  const char* terminal = terminal_get_default();
  if (terminal != NULL) {
    LOG_DEBUG("Terminal module initialized (using: %s)", terminal);
  } else {
    LOG_ERROR("%s", terminal_missing_report());
  }
}

/*
 * Check if the module is initialized (for the startup check)
 */
bool
terminal_is_initialized(void)
{
  return initialized;
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
 * Fire-and-forget: the SIGCHLD handler reaps the child when it exits, so this
 * returns without waiting for it, however long the terminal lives.
 */
bool
terminal_spawn(void)
{
  const char* term_cmd = terminal_get_default();

  /*
   * Nothing to exec. Forking a shell to run a name that is not installed only
   * adds its own "not found" line to the report, so the WM does not fork at
   * all and says what it looked for.
   */
  if (term_cmd == NULL) {
    LOG_ERROR("%s", terminal_missing_report());
    return false;
  }

  pid_t pid = fork();

  if (pid < 0) {
    LOG_ERROR("Failed to fork for terminal: %s", strerror(errno));
    return false;
  }

  if (pid == 0) {
    /* Child process - execute terminal */

    /* A terminal that keeps a copy of the X socket open leaves the server
       believing the WM is still connected after it has exited. */
    wm_xcb_close_connection_in_child();

    /* The terminal opens its own X connection: give it the server this WM is
       serving, the same way the launcher does for dmenu. */
    const char* display = wm_xcb_display_name();
    if (display != NULL && setenv("DISPLAY", display, 1) != 0) {
      LOG_ERROR("Child: failed to set DISPLAY for the terminal: %s", strerror(errno));
      _exit(1);
    }

    /*
     * Use setsid to create a new session and detach from controlling tty.
     * This prevents the terminal from receiving signals from the WM's tty.
     */
    if (setsid() < 0) {
      LOG_DEBUG("Child: setsid failed (non-fatal): %s", strerror(errno));
      /* Continue anyway - the exec will likely work */
    }

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

  LOG_DEBUG("Terminal spawned: pid=%d cmd=%s", (int) pid, term_cmd);
  return true;
}