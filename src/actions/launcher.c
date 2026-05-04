/*
 * launcher.c - Application launcher using dmenu
 *
 * Implements the "app.launch" action for launching applications via dmenu.
 * This is a system-wide action that doesn't require a target.
 *
 * Design:
 * - Action: "app.launch" with ACTION_TARGET_NONE (no target needed)
 * - Forks dmenu, waits for selection
 * - Executes selected command via fork+exec
 * - No XCB keyboard grab (dmenu handles its own grabbing)
 *
 * Security note: Command execution is intentional - user explicitly selects
 * the command via dmenu, so this is not a security issue.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <xcb/xcb.h>

#include "launcher.h"
#include "wm-log.h"
#include "wm-xcb.h"

/*
 * Action definition for app.launch
 */
static Action launcher_action = {
  .name            = "app.launch",
  .description     = "Launch an application via dmenu",
  .callback        = launcher_action_launch,
  .target_resolver = NULL,
  .target_type     = ACTION_TARGET_NONE,
  .target_required = false,
  .userdata        = NULL,
};

/*
 * Dmenu configuration
 */
static LauncherDmenuConfig dmenu_config = {
  .prompt        = "Run: ",
  .font          = "-misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1",
  .color         = "#bbbbbb",
  .sel_color     = "#005577",
  .x_offset      = 0,
  .y_offset      = 0,
  .grab_keyboard = true,
};

/*
 * Track if launcher is initialized
 */
static bool initialized = false;

/*
 * Default dmenu path
 */
#define DMENU_PATH "dmenu_path"
#define DMENU_RUN  "dmenu_run"

/*
 * Initialize the launcher module.
 * Registers the "app.launch" action with the action registry.
 */
void
launcher_init(void)
{
  if (initialized) {
    LOG_DEBUG("Launcher already initialized");
    return;
  }

  LOG_DEBUG("Initializing launcher module");

  /* Register the action */
  if (!action_register(&launcher_action)) {
    LOG_ERROR("Failed to register app.launch action");
    return;
  }

  initialized = true;
  LOG_DEBUG("Launcher module initialized");
}

/*
 * Shutdown the launcher module.
 * Unregisters the action.
 */
void
launcher_shutdown(void)
{
  if (!initialized) {
    return;
  }

  LOG_DEBUG("Shutting down launcher module");

  /* Unregister the action */
  action_unregister("app.launch");

  initialized = false;
  LOG_DEBUG("Launcher module shutdown complete");
}

/*
 * Get the current dmenu configuration.
 */
const LauncherDmenuConfig*
launcher_get_dmenu_config(void)
{
  return &dmenu_config;
}

/*
 * Set the dmenu configuration.
 * Must be called before launcher_init() or behavior is undefined.
 */
void
launcher_set_dmenu_config(LauncherDmenuConfig config)
{
  dmenu_config = config;
}

/*
 * Action callback for launching applications via dmenu.
 *
 * This function:
 * 1. Forks a child process
 * 2. Child runs dmenu to get user selection
 * 3. Parent waits for child and executes selection if non-empty
 *
 * @param inv  Action invocation (target not used for this action)
 * @return     true on successful launch, false on failure or no selection
 */
bool
launcher_action_launch(ActionInvocation* inv)
{
  (void) inv;

  if (!launcher_run_dmenu()) {
    return false;
  }

  return true;
}

/*
 * Run dmenu and execute the selected command.
 * This is a synchronous operation - it blocks until dmenu exits.
 *
 * @return  true if a command was selected and executed, false otherwise
 */
bool
launcher_run_dmenu(void)
{
  int pipefd[2];

  /* Create pipe for dmenu output */
  if (pipe(pipefd) < 0) {
    LOG_ERROR("Failed to create pipe for dmenu: %s", strerror(errno));
    return false;
  }

  pid_t pid = fork();

  if (pid < 0) {
    LOG_ERROR("Failed to fork for dmenu: %s", strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) {
    /* Child process - run dmenu */

    /* Close read end of pipe */
    close(pipefd[0]);

    /* Redirect stdout to pipe write end */
    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
      LOG_ERROR("Child: failed to redirect stdout: %s", strerror(errno));
      _exit(1);
    }
    close(pipefd[1]);

    /*
     * Build dmenu arguments
     * Using dmenu_run style: reads PATH and shows executable names
     */
    const char* prompt = dmenu_config.prompt ? dmenu_config.prompt : "Run: ";

    /* execlp looks for dmenu_run in PATH, which itself uses dmenu_path */
    execlp("dmenu_run", "dmenu_run", "-p", prompt, (char*) NULL);

    /* If execlp returns, it failed */
    LOG_ERROR("Child: failed to execute dmenu_run: %s", strerror(errno));
    _exit(1);
  }

  /* Parent process */

  /* Close write end of pipe */
  close(pipefd[1]);

  /* Read dmenu output */
  char    cmd[LAUNCHER_MAX_CMD_LENGTH];
  ssize_t nread = read(pipefd[0], cmd, sizeof(cmd) - 1);
  close(pipefd[0]);

  /* Wait for dmenu child to exit */
  int status;
  waitpid(pid, &status, 0);

  /* Check if dmenu was cancelled (exit code from dmenu is 1 for no selection) */
  if (nread <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    /* No selection or cancelled */
    if (nread > 0) {
      /* Read any remaining data */
      while (read(pipefd[0], cmd, sizeof(cmd)) > 0)
        ;
    }
    LOG_DEBUG("dmenu: no selection or cancelled");
    return false;
  }

  /* Null-terminate the command */
  cmd[nread] = '\0';

  /* Remove trailing newline if present */
  size_t len = strlen(cmd);
  if (len > 0 && cmd[len - 1] == '\n') {
    cmd[len - 1] = '\0';
  }

  /* Skip empty commands */
  if (cmd[0] == '\0') {
    LOG_DEBUG("dmenu: empty selection");
    return false;
  }

  LOG_DEBUG("dmenu selected: %s", cmd);

  /*
   * Execute the selected command
   * Use fork+exec to avoid affecting the window manager
   */
  pid = fork();

  if (pid < 0) {
    LOG_ERROR("Failed to fork for command execution: %s", strerror(errno));
    return false;
  }

  if (pid == 0) {
    /* Child - execute the command */

    /*
     * Use execl with sh -c for proper shell interpretation.
     * This allows commands with arguments to work correctly.
     * The command was already selected from dmenu (which shows executable
     * names only), so we execute it directly without shell interpretation
     * to avoid security issues with special characters.
     *
     * For simple commands like "firefox", execl works directly.
     * For commands with arguments, users should use the shell explicitly
     * by prefixing with a shell command.
     */

    /* Try direct execution first (for simple commands like "firefox") */
    execlp(cmd, cmd, (char*) NULL);

    /* If execlp fails, try with sh -c (for built-in commands) */
    execlp("sh", "sh", "-c", cmd, (char*) NULL);

    /* If we get here, both exec attempts failed */
    _exit(1);
  }

  /* Parent - wait for the command to start (or fail quickly) */
  int   cmd_status;
  pid_t waited = waitpid(pid, &cmd_status, WNOHANG);

  if (waited < 0) {
    LOG_ERROR("waitpid failed: %s", strerror(errno));
    return false;
  }

  if (waited == 0) {
    /* Command is running in background */
    LOG_DEBUG("Command started: %s", cmd);
  } else if (WIFEXITED(cmd_status) && WEXITSTATUS(cmd_status) == 0) {
    LOG_DEBUG("Command completed immediately: %s", cmd);
  } else {
    LOG_DEBUG("Command execution returned: %s (status=%d)", cmd, cmd_status);
  }

  return true;
}