/*
 * launcher.c - Application launcher using dmenu
 *
 * Implements the "app.launch" action for launching applications via dmenu.
 * This is a system-wide action that doesn't require a target.
 *
 * Design:
 * - Action: "app.launch" with ACTION_TARGET_NONE (no target needed)
 * - Forks dmenu, then returns: the selection is read by the event loop
 * - Executes the selected command via fork+exec
 * - No XCB keyboard grab: dmenu grabs the keyboard itself
 *
 * Security note: Command execution is intentional - user explicitly selects
 * the command via dmenu, so this is not a security issue.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#include <xcb/xcb.h>

#include "launcher.h"
#include "wm-hub.h"
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
  .prompt    = "Run: ",
  .font      = "-misc-fixed-medium-r-normal--13-120-75-75-C-70-iso10646-1",
  .color     = "#bbbbbb",
  .sel_color = "#005577",
  .x_offset  = 0,
  .y_offset  = 0,
};

/*
 * Track if launcher is initialized
 */
static bool initialized = false;

/*
 * The prompt dmenu shows when the configuration names none.
 */
#define DMENU_DEFAULT_PROMPT "Run: "

/*
 * The dmenu that is open, if any.
 *
 * One at a time: a second MOD+p while the prompt is up is a no-op, because two
 * prompts would fight over the keyboard. The buffer is fixed so the pipe can be
 * read from the event loop without allocating on that path.
 */
static struct {
  bool   active;
  int    fd;
  size_t length;
  char   command[LAUNCHER_MAX_CMD_LENGTH];
} selection = { .active = false, .fd = -1, .length = 0, .command = { 0 } };

static void launcher_pipe_readable(int fd, void* userdata);

/*
 * Initialize the launcher module.
 * Registers the "app.launch" action with the action registry.
 */
void
launcher_init(void)
{
  /* The registry can be dropped and rebuilt underneath this module without
   * launcher_shutdown() running: keybinding_shutdown() clears it directly. The
   * flag would then claim an action that is no longer registered, and the
   * second startup would leave app.launch missing. */
  if (initialized && action_exists(launcher_action.name)) {
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
 * Check if the module is initialized (for the startup check)
 */
bool
launcher_is_initialized(void)
{
  return initialized;
}

/*
 * Whether a dmenu is open and its pipe is being watched by the event loop.
 */
bool
launcher_is_running(void)
{
  return selection.active;
}

/*
 * Shutdown the launcher module.
 * Unregisters the action and stops watching an open dmenu.
 */
void
launcher_shutdown(void)
{
  if (!initialized) {
    return;
  }

  LOG_DEBUG("Shutting down launcher module");

  if (selection.active) {
    /* Closing the read end is what tells a dmenu nobody is listening any more:
       it gets EPIPE on its next write and exits. Its own reaping is the SIGCHLD
       handler's job. */
    wm_loop_remove_fd_handler(selection.fd);
    close(selection.fd);
    selection.active = false;
    selection.fd     = -1;
    selection.length = 0;
  }

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
 * Starts dmenu and returns. The selection arrives through the event loop, so
 * the loop keeps running while the prompt is open. dmenu holds the keyboard
 * grab for that time, so keys reach it rather than the WM.
 *
 * @param inv  Action invocation (target not used for this action)
 * @return     true when dmenu was started, false when it could not be started
 *             or a prompt is already open
 */
bool
launcher_action_launch(ActionInvocation* inv)
{
  (void) inv;

  if (launcher_is_running()) {
    /* Two prompts would fight over the keyboard, so the second is refused. It
       is not a failure: the open prompt still owes its own selection or
       cancellation, and that is what EVT_LAUNCHER_FAILED reports. */
    LOG_DEBUG("dmenu is already open: ignoring the second launch");
    return false;
  }

  if (!launcher_run_dmenu()) {
    /* No subscriber needs to know why: the action's return value carries the
       failure to the caller, and this event is for the components that show
       the user that the request was not honoured. */
    hub_emit(EVT_LAUNCHER_FAILED, TARGET_ID_NONE, NULL);
    return false;
  }

  return true;
}

/*
 * Runs the selected command in its own process.
 * Does not wait for it: the SIGCHLD handler reaps it when it exits.
 */
static bool
launcher_spawn_command(const char* command)
{
  pid_t pid = fork();

  if (pid < 0) {
    LOG_ERROR("Failed to fork for command execution: %s", strerror(errno));
    return false;
  }

  if (pid == 0) {
    wm_xcb_close_connection_in_child();

    /*
     * Try direct execution first (for simple commands like "firefox"). The
     * fallback through sh -c covers a pipeline or a built-in that execlp
     * cannot find. The command was chosen by the user from the list dmenu
     * showed.
     */
    execlp(command, command, (char*) NULL);
    execlp("sh", "sh", "-c", command, (char*) NULL);

    LOG_DEBUG("Child: failed to execute '%s': %s", command, strerror(errno));
    _exit(1);
  }

  LOG_DEBUG("Command started: %s", command);
  return true;
}

/*
 * Stops watching the dmenu pipe and acts on what it wrote.
 */
static void
launcher_finish_selection(void)
{
  selection.active = false;
  wm_loop_remove_fd_handler(selection.fd);
  close(selection.fd);
  selection.fd = -1;

  selection.command[selection.length] = '\0';

  /* dmenu writes the selected entry and a newline; a cancelled prompt writes
     nothing at all and closes the pipe. */
  size_t len = strlen(selection.command);
  while (len > 0 && (selection.command[len - 1] == '\n' || selection.command[len - 1] == '\r'))
    selection.command[--len] = '\0';

  if (len == 0) {
    LOG_DEBUG("dmenu: no selection or cancelled");
    hub_emit(EVT_LAUNCHER_FAILED, TARGET_ID_NONE, NULL);
    return;
  }

  LOG_DEBUG("dmenu selected: %s", selection.command);
  launcher_spawn_command(selection.command);
}

/*
 * Event loop callback: dmenu has written something, or has closed its end.
 *
 * Reads what is available and returns at once. End of file means dmenu is
 * done: a selection is there, or the user cancelled.
 */
static void
launcher_pipe_readable(int fd, void* userdata)
{
  (void) userdata;

  if (!selection.active || fd != selection.fd)
    return;

  for (;;) {
    size_t room = sizeof(selection.command) - 1 - selection.length;
    if (room == 0)
      break;

    /* allow-blocking: the read end is O_NONBLOCK, so this returns EAGAIN
       instead of waiting for dmenu to write. */
    ssize_t nread = read(fd, selection.command + selection.length, room);

    if (nread > 0) {
      selection.length += (size_t) nread;
      continue;
    }
    if (nread < 0 && errno == EINTR)
      continue;
    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return; /* dmenu is still open and may write more */
    break;    /* 0: end of file, or an error that leaves nothing to read */
  }

  launcher_finish_selection();
}

/*
 * Start dmenu and return immediately.
 *
 * The read end of the pipe is handed to the event loop, so the WM keeps running
 * its loop until dmenu writes a selection or exits. dmenu itself holds the
 * keyboard grab while it is up, so the keys a user presses then go to it.
 *
 * @return  true when dmenu was started, false otherwise
 */
bool
launcher_run_dmenu(void)
{
  if (selection.active) {
    LOG_DEBUG("dmenu is already open: ignoring the second launch");
    return false;
  }

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

    wm_xcb_close_connection_in_child();

    const char* prompt = dmenu_config.prompt ? dmenu_config.prompt : DMENU_DEFAULT_PROMPT;

    /* dmenu_run does not print the choice: it backgrounds
       `dmenu_path | dmenu "$@" | $SHELL`, so the shell inside the script runs
       the selection and this pipe carries whatever that program writes. Run
       dmenu as the last stage here instead, with the list on its stdin: the
       pipe then carries the selected line, and it reaches its end when the
       menu closes. The selected command is run by launcher_spawn_command(),
       which is the WM's own fork. */
    execlp("sh", "sh", "-c", "dmenu_path | dmenu -p \"$1\"", "sh", prompt, (char*) NULL);

    /* If execlp returns, it failed */
    LOG_ERROR("Child: failed to execute dmenu: %s", strerror(errno));
    _exit(1);
  }

  /* Parent process */

  /* Close write end of pipe */
  close(pipefd[1]);

  /* The event loop polls this descriptor and must never wait on it. */
  int flags = fcntl(pipefd[0], F_GETFL, 0);
  if (flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG_ERROR("Failed to make the dmenu pipe non-blocking: %s", strerror(errno));
    close(pipefd[0]);
    return false;
  }

  /* A program the WM starts while the prompt is open must not inherit the
     read end: it would hold that descriptor for as long as it runs. Closing
     the write end here, and leaving it to dmenu in the child, is what delivers
     the end of file when the menu closes. */
  flags = fcntl(pipefd[0], F_GETFD, 0);
  if (flags >= 0)
    (void) fcntl(pipefd[0], F_SETFD, flags | FD_CLOEXEC);

  selection.active     = true;
  selection.fd         = pipefd[0];
  selection.length     = 0;
  selection.command[0] = '\0';

  if (!wm_loop_add_fd_handler(selection.fd, launcher_pipe_readable, NULL)) {
    LOG_ERROR("Failed to watch the dmenu pipe: the launcher is not started");
    selection.active = false;
    close(selection.fd);
    selection.fd = -1;
    return false;
  }

  LOG_DEBUG("dmenu started: pid %d, the selection arrives through the event loop", (int) pid);
  return true;
}
