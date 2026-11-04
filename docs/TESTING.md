# Testing the Window Manager

*Written 2026-11-01. Numbers in this file were measured on x86_64-linux with the dev shell below.*

> **Bottom line:** `make test` covers the model, `make test-x` covers the seam
> between the WM and the X server, `scripts/perf-test.sh` covers cost. Both start their own nested X
> server, so they need no second machine and no root. Without arguments they
> print PASS/FAIL lines and leave logs under `$TMPDIR` (`wm-xephyr-test/`,
> `wm-perf-test/`).

---

## 0. What runs automatically

`lefthook` runs the gates on every commit and push; CI runs the same commands.
The seam suite (`make test-x`) is one of the push gates, through
`scripts/check-seam.sh`: it runs on a private Xvfb it starts itself, whatever
`DISPLAY` says, and fails rather than skipping when that server cannot start.
See [QUALITY.md](QUALITY.md). The sections below are the layers you run by hand
while working.

## 1. Environment

The dev shell comes from `flake.nix` via direnv:

Run these from the repository root.

```bash
direnv allow          # first time, or after editing flake.nix
# alternative without direnv:
nix develop
```

The shell provides gcc, clang, clang-tools, bear, gdb, the libxcb family, and
the test tools: `Xephyr` (from `xorg-server`), `xvfb`, `xdotool`, `xprop`,
`xwininfo`, `xrandr`, `xmodmap`, `xterm`, `dmenu`, `imagemagick` (`import` for
screenshots), `valgrind`, and `strace`.

A tool the harnesses need but the shell lacks is fixed in `flake.nix`, in the
same commit as the recipe that needs it. Each worktree carries its own copy of
`flake.nix`, so the tool appears in that worktree after `direnv allow` or
`nix develop` again, not in the shell that is already running. `scripts/check-flake.sh`
evaluates the shell without entering it, which is what catches a broken
`flake.nix` before the next session finds it; the push gate runs the same check.

Rule of thumb: anything you run against the WM needs the same shell the WM was
built in, because the binary links the store paths of that shell's libraries.

`LIBXCB_SRC` points at libxcb's unpacked source. The header declares
`xcb_poll_for_reply()` and says nothing about whether it waits, and the serial
it compares a reply against is set in `src/xcb_out.c` — the answer to either
question is in `src/xcb_in.c` and `src/xcb_out.c`, not in the header and not in
a man page. Read them from `$LIBXCB_SRC`; fetching the same files from gitlab
needs the network and fails without it.

```bash
rg -n 'poll_for_reply' "$LIBXCB_SRC/src/xcb_in.c"   # does it block
rg -n 'REQUEST_CHECKED' "$LIBXCB_SRC/src/xcb_out.c" # which bit the serial masks
```

### Container image

The `Dockerfile` builds the WM in a Nix builder and runs it under `Xvfb` with
`x11vnc`, so there is a display on a machine with no X server of its own. Three
make targets drive it:

```bash
make container-build   # docker build -t wm .
make container-run     # detached, publishes port 5900
make container-clean   # removes the image
```

The entry point, `start.sh`, is written by `flake.nix`: it starts `Xvfb :99`, an
`xterm`, the WM, and `x11vnc` on that display. Connect a VNC client to
`localhost:5900`. The image sets no VNC password, so `docker logs` carries
x11vnc's warning about that. The image holds the WM built from the tree at
image-build time, so a source change needs `make container-build` again; the
image is not how a test run should pick up an edit.

---

### Coverage and fuzzing

`scripts/check-coverage.sh` says what the unit suite executes, per file and per
branch; `scripts/fuzz.sh` runs the two libFuzzer harnesses against the X event
dispatcher and the window-rule matcher. `docs/QUALITY.md` has both, what the
baseline means, and why a 0% file is the number worth reading.

### A test that early-returns is not coverage

A unit test whose subject gives up for lack of an X connection asserts on a
fallback branch, not on the behaviour the bead was about. Two monitor-manager
tests did this and read as coverage of the async RandR pass for a whole cycle;
the defect the pass carried survived a green suite.

When the subject cannot run in a unit test, say which branch the test covers in
its name (`..._sync_fallback`), assert that the other branch did not start (a
`*_for_tests()` getter under `WM_HUB_TESTING` is the usual way), and cover the
real path in `tests/x/`.

## 2. Unit tests

```bash
make            # normal build, -Os
make test       # builds and runs test/, prints "Total: N passed, M failed"
```

`make test` links every non-`main` object plus the `tests/unit/test-*.c` files and runs
them. The runner prints the current count and must report zero failures.
Components are tested
without an X server by calling their handlers with fabricated events and
inspecting hub events; that is the pattern to copy for new components.

The commit and push gates run the same suite through `scripts/check-unit.sh`,
which keeps the log to itself and prints it only when the run fails. Run
`make test` by hand when you want the log of a passing run.

The gate reads `DEBUG` and `SANITIZE` the way `make` does, and the pass line
names the variant it built. The debug gate runs it as
`DEBUG=1 scripts/check-unit.sh`, so a failing assertion in the `-pedantic`
build arrives with the run instead of make's `Error 1` line alone.

Two extra targets:

```bash
make test-sm-standalone    # state machine framework only, prints DEBUG trace
make test-standalone       # hub registry and event bus only
```

### Debug build

```bash
make DEBUG=1
```

Adds `-g3 -Og -Wall -pedantic -DDEBUG`, which turns `LOG_DEBUG()` into real
output. This is the build to use when you need to see what the WM decided.
`-pedantic` rejects GNU-only constructs; a zero-length array initializer in
`tiling.c` used to break this build and was fixed on 2026-11-01. If the debug
build stops compiling, treat that as a bug: it is the only way to read the
event log.

The nested-server scripts run the variant you name, so the debug log is one
command away:

```bash
make DEBUG=1                       # build the debug WM, or let the scripts do it
DEBUG=1 scripts/xephyr-test.sh --verbose
make DEBUG=1 test-x                # the seam suite against the debug WM
DEBUG=1 ./tests/x/run.sh           # the same thing spelled out
```

`scripts/xephyr-test.sh` and `tests/x/run.sh` print two lines before they start
anything: the parent display they will draw into, then the build they will run.

```
xephyr-test.sh: parent display = :99 (private Xvfb), session display = :0
xephyr-test.sh: variant=debug (DEBUG=1 SANITIZE=0), binary=<repo>/build/debug/wm, log=<tmpdir>/wm-xephyr-test/wm.log
```

The variant, the binary that runs and the log it writes are in the variant
line, so a run never leaves you guessing which build it used or where its output
went. If that line says `variant=debug` and the log is still empty, the WM
itself never logged and the binary is not the suspect. `SANITIZE=1` works the
same way.

The WM flushes each log line as it writes it, so a log from a WM that was killed
rather than shut down is complete up to the last line it wrote. That matters for
the harnesses, which stop the WM with a signal, and it means the log needs no
`stdbuf` wrapper: the sanitize builds cannot have one anyway, because ASan
refuses to start when another library is preloaded ahead of its runtime.

### Build directories

Each flag set compiles into its own directory under `build/`: `release`,
`release-sanitize`, `debug`, `debug-sanitize`. So `make`, `make SANITIZE=1` and
`make DEBUG=1` can be alternated in one checkout with no `make clean` in
between, and no build reuses another variant's objects. Whatever was built
last by `make` is copied to `./wm`, which is what a manual `./wm` and the flake
read. `scripts/xephyr-test.sh` and `tests/x/run.sh` do not: they run
`build/<variant>/wm` and leave `./wm` alone. `make clean` removes every variant.

The test binary has no copy at the tree root. `make` and `make test` remove a
`./test` left by an earlier build, because that copy is no longer refreshed and
running it would print a count from the tree that produced it. Run the suite
through `make test`, or name the variant binary directly when you want to put
it under `valgrind`.

The suite installs the WM's own signal handlers and raises `SIGTERM` (and
`SIGUSR1`, for the state dump) against itself, so a debugger stops there first
and reports a signal that is not a crash. Pass them through:

```bash
gdb -batch -ex 'handle SIGTERM nostop noprint pass' \
          -ex 'handle SIGUSR1 nostop noprint pass' \
          -ex run -ex bt build/debug/test
```

The copies `./wm` gets are replaced by writing a temporary file and renaming it
over `./wm`, so a `make` while a WM started from `./wm` is still running exits 0
instead of failing on a busy file. The running WM keeps the binary it started
with: restart it before you read its log and expect the change to be in it.

`scripts/check-build-variants.sh` is the gate for this (see `docs/QUALITY.md`).

---

## 3. Seam tests

```bash
make test-x                 # all of tests/x/test-*.sh
make test-x TEST_X_ARGS="--filter=tag"   # one file
make test-x TEST_X_ARGS="--keep"         # leave the session running
make test-x TEST_X_ARGS="--verbose"      # print the WM log for each failure
make DEBUG=1 test-x         # the same suite against the debug WM
make SANITIZE=1 test-x      # the same suite against the sanitized WM
```

A seam test asserts what the X server sees, not what the WM thinks: the window
is viewable, `WM_STATE` is `Normal`, two windows do not overlap, the input focus
is where the WM said it would be, `_NET_CLIENT_LIST` exists. Those are the
assertions the unit tests cannot make.

`scripts/xephyr-test.sh` is the earlier single-scenario version of the same idea;
prefer `make test-x`, which also runs before every push, and keep the old script
only because several beads issues name it.

`tests/x/run.sh` starts one Xephyr for the whole run, and a WM of its own for
every test file. It runs each file as a separate process with `DISPLAY`,
`WM_PID`, `WM_LOG` and `ROOT_DIR` exported, `WM_PID` naming that file's WM.
The Xephyr is shared and the WM is not, because the state that leaks between
files is the WM's: a tag view, a client moved to another tag, the input focus,
the layout. A file that inherits that state fails on something it never did, and
the failure names the wrong test. `test-94-fresh-wm-per-file.sh` asserts it,
against the WM's own start time in `/proc`. The server's own state is still
shared, and that is the part a test has to put back.

`tests/x/lib.sh` has the helpers: `spawn_client`, `wait_viewable`,
`require_visible`, `rect_of`, `overlap_pairs`, `focused_window`, `press`,
`root_prop`, and the `check_*` assertions.

Two kinds of state a fresh WM does not reset, because they live in the X server
rather than in the WM: the lock modifiers (NumLock, CapsLock) and the RandR
mode. A test that turns one of those on turns it off on every exit path.
`test-45-key-grab.sh` is the example: it turns NumLock on to prove a grabbed key
still fires, and turns it off before its assertions are read.

A test that needs a preamble to reach the state it asserts says in its header
why the preamble is there, so the next reader can tell a setup step from a
workaround for the state another file left behind. A preamble that outlives its
reason is the thing to delete.

A failure listed in `tests/quality/seam-baseline.txt` is a **known** failure: it
is reported and does not fail the run. A failure missing from that file is
**new** and exits non-zero, which is what the CI seam job watches. The suite
therefore needs the baseline to be useful: without it, every run is red and a
regression is invisible next to the standing list of unwired features.

```bash
make test-x TEST_X_ARGS="--update-baseline"   # accept today's failures
scripts/check-sanitize.sh --update-baseline  # accept today's leak sites
scripts/check-sanitize-x.sh --update-baseline  # accept today's sites under a live WM
```

Layout: the unit tests live in `tests/unit/`, the seam tests in `tests/x/`, and
the reviewed baselines in `tests/quality/`. The repository root is the window
manager itself.

Refresh it when an epic lands, and read the diff: an entry that disappears is
progress, an entry that appears is either a new defect or a test that got
stricter. The remaining known failures, as of the last refresh:

| Test | Known failure, because |
|------|-------------------------|
| `test-53-tag-move-client` | `tag-manager.client-move` toggles the tag bit instead of moving the client, so the client stays on the visible tag (wm-rh6.7) |
| `test-55-randr-relayout` | an xrandr mode change never reaches the WM: the RandR handler is registered under a reply code, and a monitor geometry change does not ask for a new layout (wm-4f6.19) |
| `test-60-ewmh-root-properties` | no root properties are set |
| `test-75-pointer-drag` (two assertions) | there is no pointer component yet, so nothing handles BUTTON_PRESS, MOTION_NOTIFY or BUTTON_RELEASE (wm-rh6.3) |

Write a test so that it cannot pass for the wrong reason: a precondition such as
"the window is visible before I press the key" uses `require_visible`, which
stops the test, not `check_eq`, which would let a later "the window is gone now"
pass because it was never there.

These files start their own display, because the condition they check cannot
be produced once the WM is already up: `test-86-queued-event.sh`,
`test-88-adopt-existing-windows.sh`, and `test-54-config-file-binding.sh`.
`test-102-probe-shape.sh` starts one too, by running `tests/x/probe.sh`, so the
probe does not take the display the suite is using.

`run.sh` points `WM_CONFIG` at a path that does not exist, so the suite always
meets the compiled bindings whatever the developer running it has in
`~/.config/wm/config`. `test-54-config-file-binding.sh` is the one that wants a
file: it starts a bare Xvfb, writes a file that binds `MOD+F5` and `MOD+F6` (keys
the compiled configuration does not have) and holds one line the parser must
reject, and starts the WM with it. It asserts the file's keys change the view,
that a compiled key (`MOD+2`) no longer does, and that the WM log reports the
rejected line once by line number. Then it restarts the WM with no file and
asserts `MOD+2` works again, which is the fallback.

`test-55-randr-relayout.sh` uses the shared session but changes its screen: it
switches the nested output to the largest mode narrower than the current one and
switches back on every exit path, because the tests after it tile into whatever
screen is left. A mode the server refuses stops the test as a precondition, so
a failure it reports is about the WM's answer to the change.

`test-86-queued-event.sh` checks what a queued event does. libxcb reads ahead of
`poll(2)`, so an event can sit in its queue while the socket is no longer
readable; the test holds the first EWMH atom query with
`tools/xcb-ewmh-delay.so` (an `LD_PRELOAD` shim), maps a window while the WM is
held, then releases it. The reply wait reads that `MapRequest` into libxcb's
queue, and a loop that only polls the socket never dispatches it. The shim is
built by `make test-x`; it reads `WM_QUEUE_TEST_MARKER` and `WM_QUEUE_TEST_GO`
and does nothing when either is unset.

`test-88-adopt-existing-windows.sh` checks the startup scan: it maps two windows
and an override-redirect window on a bare display, starts the WM afterwards, and
asserts that the two are tiled and carry `WM_STATE=Normal` while the
override-redirect window keeps its geometry and gets no `WM_STATE`.

`test-51-burst-tag-switch.sh` uses the suite's WM but stops it with `SIGSTOP`
while it sends `super+2` and `super+1`, so both key presses are queued and the
WM dispatches them in one drain. That is the ordering where a hide and a later
show both run before the `UnmapNotify` the hide causes is read. The test resumes
the WM on every exit path. `test-52-queued-backlog-responsiveness.sh` stops it
the same way but queues 200 tag toggles and one `super+1`, then measures the wall
clock from the resume until that `super+1` has brought the window back: the
answer to a key cannot be stuck behind the backlog.

`run.sh` puts `tests/fakes/` at the front of `PATH`. The fake `dmenu` records
its start in `$WM_FAKE_STARTED_DIR` and then sleeps, writing nothing, which is a
prompt nobody has answered yet; the fake `dmenu_path` records its start and
prints nothing. A test that presses `MOD+p` wants a child that holds the pipe
open and leaves the keyboard alone: a real dmenu grabs the keyboard for as long
as it is open, so `MOD+2` would go to the prompt and never reach the WM.
`test-87-dmenu-prints-the-selection.sh` runs the installed dmenu for the part a
stand-in cannot answer for, what it writes on stdout.
`test-92-launcher-does-not-block.sh` uses the fakes: it waits for the dmenu
marker so an unstarted launcher cannot make the assertion pass on an idle WM,
then requires `MOD+2` to hide the tag-1 window within 500 ms. It also requires
the `dmenu_path` marker. A missing marker fails the test; the message does not
name whatever else the launcher exec'd. The test also requires every file in
`tests/fakes/` to have left a marker, so a stand-in the launcher did not exec
fails by that stand-in's name. Verified both ways on this
suite: with the async launcher the key lands in tens of milliseconds; with the
launcher's blocking read and `waitpid()` put back, the same assertion fails with
"MOD+2 did not reach the WM within 500 ms". `scripts/perf-test.sh` runs the same
stage with the same directory on `PATH`, and fails the run when the key goes
unanswered. What the stage measures is that the WM's loop still answers, which
is the promise; a stand-in that does not grab the keyboard is what lets the
test see it.

---

## 3a. Stand-ins for outside programs

A test that needs a program it does not want to run stands in for it, and the
stand-in is a claim about a program the test never ran. Nothing inside a suite
can falsify that claim, because the fake answers every assertion the test asks
it. The launcher shipped with three fakes of `dmenu_run`, a script that
backgrounds its pipeline and exits, while the WM exec'd that script and waited
for a selection that never arrived: both suites were green on the wrong program,
and the defect took a `command -v` and a `cat` to find. The rules below are what
stops a repeat.

- **A stand-in names the program it models**, on the line after the shebang:
  `# models: <program>, <what the real program does and what this one does
  instead>`. The word after the colon is the program, and it is also the file
  name, because PATH shadowing finds a program by its file name.
- **It cites a line of that program**, on the next line: `# from <program>:
  <a line of the installed file, verbatim>`. The line is one the installed
  file has, and a line that is not there fails the gate: the claim is checked
  against the program, not against the stand-in's own description. The citation
  proves the stand-in was written against that file; what the program does with
  a selection is checked by the case that runs it, below.
- **The named program is installed, and the cited line is in it.**
  `scripts/check-fakes.sh` resolves the name with every fake directory removed
  from `PATH`, requires the result to be a regular file (a directory answers to
  a name and has no contents), and looks for the cited line in it. The path is
  resolved by the check rather than written into the stand-in: a Nix store path
  is a property of the machine that built the shell, and the check prints the
  path it found. An upgrade that changes the cited line fails the gate, which is
  the moment to read the program again rather than keep modelling the version
  before it.
- **A stand-in records that it ran**, writing
  `$WM_FAKE_STARTED_DIR/<its name>` as its first act.
  `test-92-launcher-does-not-block.sh` requires every file in `tests/fakes/` to
  have left one, so a stand-in for a program nothing execs fails as unreached
  instead of shadowing a name in silence.
- **Where the real program can run, one case runs it.** `dmenu_path` needs no
  display: `tests/unit/test-launcher.c` runs the installed one and asserts the
  prompt received a list; the case fails rather than skipping when the program
  is not on `PATH`. The installed `dmenu` draws with Xft, which the nested
  server does provide (it is `xterm`'s bitmap fonts that are missing), so
  `tests/x/test-87-dmenu-prints-the-selection.sh` runs it on the suite's server:
  a three-line list on stdin, a key press through the grab, and the assertion
  that stdout holds the chosen line and the program exited 0. Not through the
  WM, because a real prompt keeps the keyboard for as long as it is open;
  `test-92` measures the loop with the stand-in.

`scripts/check-fakes.sh` checks the first three, and the seam test checks the
fourth. The fifth is a judgement each stand-in has to answer for: a fake of a
program no case ever runs stands in for an idea of it.

The stand-ins for the harnesses live in `tests/fakes/`, one file per program, and
`tests/x/run.sh` and `scripts/perf-test.sh` both put that directory at the front
of `PATH`. One directory is what stops the two harnesses' fakes drifting apart,
and a program the code starts exec'ing arrives as a file in the diff. A unit test
that needs a different body per case writes its own fake and states what it
models the same way; `test_launcher_fake_names_the_program_it_models` reads that
claim back, resolves the program behind the fake, and looks for the cited line
in the file it resolves to.

---

## 3b. Static checks

```bash
make check-async            # scans the files the build compiles
make check-async V=-v       # also list the waived calls and why
```

`scripts/check-async.sh` enforces `docs/PHILOSOPHY.md` 2.3 mechanically. It asks
the build for its source list (`make -s print-src`) so a file that is not
compiled cannot produce findings, then flags calls that block:

| Pattern | Why it is flagged |
|---------|-------------------|
| `xcb_*_reply()`, `xcb_wait_for_reply()`, `xcb_request_check()`, `xcb_aux_sync()` | synchronous round trip: the loop waits for the server |
| `xcb_*_replies()` | the plural form of the same wait, once per atom, screen or list; `xcb_ewmh_init_atoms_replies()` is the one in the tree |
| `xcb_key_symbols_get_keycode()`, `xcb_key_symbols_get_keysym()` | a round trip behind a name that never says `reply`: both call `xcb_get_keyboard_mapping_reply()`. `xcb_key_symbols_alloc()` only sends the request |
| `read()` | a callback must not wait on a child's pipe |
| `waitpid()` without `WNOHANG` | blocking wait on a child |
| `sleep()`, `usleep()`, `nanosleep()` | anything that sleeps stops every client |

`xcb_poll_for_reply()`, `xcb_poll_for_reply64()` and `xcb_discard_reply()` are
not violations. Their names end in `_reply` like the calls that wait, and
libxcb documents all three as returning without waiting; flagging them would
push the author to waive the very call the rule asks for.

The rules run over a copy of each file with its comments and string contents
blanked, so a call name in prose is not a call. `scripts/lib/async-scan.sh`
writes that copy and keeps the file's line structure, so the line a rule
reports is the line to open. Without it a `/* ... */` block that began a
continuation line with `waitpid(` was reported as a blocking wait, and the
workaround was to reword the sentence.

`tests/x/test-101-async-rules.sh` pins every rule and every exception above on
probe files: one blocking call per line must be reported, and a `//` comment, a
string literal, a block comment and a `poll_for_reply` call must not.

`xcb_poll_for_event()` is not on the list. It returns NULL instead of waiting,
which is what the loop does to drain the events libxcb queued after `poll(2)`
reported the connection readable, and a grep cannot tell that from the spin it
replaced. Busy-waiting is measured instead: `tests/x/test-85-idle-cpu.sh` in the
seam suite, and the idle CPU line of `scripts/perf-test.sh`.

A call is waived when it, or the two lines above it, carries
`allow-blocking: <reason>`. The marker is the record of why the wait is safe:
startup code that runs before the loop exists, or the non-blocking read of the
signal wakeup pipe in `signals_drain_wakeups()`. The reason has to be on the
marker's own line: an `allow-blocking:` with nothing after it is reported as a
waiver without a reason and counts as a violation.

The marker is refused when an XCB handler can reach the function it sits in.
The reason is always a claim about when the code runs, and code a handler
reaches runs inside the loop. `scripts/lib/async-scan.sh` reads that from the
sources: the handlers named in an `xcb_handler_register()` call, plus everything
they reach through a call written in the tree. A comment that mentions a
function name inside another function body counts as a call, so the reading
errs toward refusing; a callback invoked through a struct field is not
followed. The refusal prints the function's name and the call is then judged as
a violation, so the fix is either to stop the handler reaching the wait or to
baseline it. `tests/x/test-100-async-scan.sh` pins the decision on small
sources, including a handler in one file reaching a helper in another.

The script prints the current counts. There are no violations left: the
baseline that held them is empty, and every remaining wait carries
`allow-blocking` with a reason. That reason is a startup path, where the loop
does not exist yet, or a read of an `O_NONBLOCK` pipe.

The launcher's read of dmenu's pipe is waived rather than excused: the read end
is `O_NONBLOCK`, so it returns `EAGAIN` instead of waiting for dmenu to write,
and the descriptor is one of the loop's event sources. There is no `waitpid()`
in the launcher at all: the `SIGCHLD` handler reaps every child.

The check is meant to reach zero by removing the violations, not by adding
markers. A new marker needs a reason that would satisfy a reviewer: "startup,
before the loop runs" is one; "it is probably fine" is not.

---

## 4. Nested X server (Xephyr)

Xephyr renders a second X server inside a window on your real session. The WM
under test runs as the only WM on that second display.

### The parent display

Xephyr needs a parent display to draw its window into. `scripts/xephyr-test.sh`,
`tests/x/run.sh`, `tests/x/probe.sh`, `scripts/check-seam.sh` and
`scripts/check-sanitize-x.sh` get a private Xvfb for it, started by the run
itself, whether or not `DISPLAY` is set: no Xephyr window appears on the desktop
you are working on, and the run answers the same on a display with another depth
or a wedged server. The rule lives in `scripts/lib/private-display.sh`, and each
run prints the display it used:

```
run.sh: parent display = :99 (private Xvfb), session display = :0
```

`tests/x/run.sh` also writes that line to `$TMPDIR_SEAM/parent-display.txt`, and
`tests/x/test-91-private-parent-display.sh` fails when the display the run
recorded is not the one it handed to its tests, or when it is the session's own.

The escape hatch is `WM_USE_SESSION_DISPLAY=1`. It is for looking at a run by
eye, and it also needs `--keep`, because the private server is killed with the
run and a kept session on it would have nothing to draw into:

```bash
WM_USE_SESSION_DISPLAY=1 make test-x TEST_X_ARGS=--keep
WM_USE_SESSION_DISPLAY=1 scripts/xephyr-test.sh --keep --verbose
```

`xvfb-run` and `Xvfb` come from the dev shell. Without them the scripts stop
with a message naming what is missing rather than falling back to the session
display: that fallback is what this replaced.

### One-shot probe

```bash
tests/x/probe.sh -n 2 -c 'xdotool mousemove 100 100'
make probe PROBE_ARGS="-n 3"
```

`make probe` runs the same script. `PROBE_ARGS` is passed through as its
arguments.

This answers "what does the WM do here?" in one call: it starts a private
Xvfb, draws a Xephyr into it, builds the WM through `scripts/lib/wm-variant.sh`,
maps `-n` windows, runs one command with `-c`, and prints the state a seam
assertion reads. Unless `WM_CONFIG` is already set, it points that variable at
a path that does not exist, the same way `tests/x/run.sh` does, and the header
names the path in force.

A standalone run prints one line before this block, from the parent-display
helper, saying it is starting a private Xvfb. The display number, the window
ids, the pid and the log body vary. The lines, in that order, and the columns
do not. The log header contains the `-l` value.

```
probe.sh: parent display = :99 (private Xvfb), session display = :0
probe.sh: variant=release (DEBUG=0 SANITIZE=0), binary=..., log=...
probe: display=:2 config=/tmp/nix-shell.../wm-probe/no-config-file
--- command: xdotool mousemove 100 100 ---
command: exit 0
--- state ---
screen: 1280 800
window          x     y     w     h  border  viewable  focus  tags  title
0x00400000    640     0   638   798       1      yes     no     -  probe-1
0x00400001      0     0   638   798       1      yes    yes     -  probe-2
viewable: 2 of 2
--- root properties ---
_NET_SUPPORTING_WM_CHECK: not set
_NET_CLIENT_LIST: not set
_NET_CURRENT_DESKTOP: not set
_NET_NUMBER_OF_DESKTOPS: not set
_NET_DESKTOP_NAMES: not set
--- wm ---
wm: running (pid 1234)
--- wm log (last N lines) ---
```

`-n`, `-W`, `-H` size and count the windows, `-w MS` is the settle wait after
the windows are mapped and again after `-c`, and `-l LINES` is the size of the
log tail. `probe.sh -h` has the list. The windows come from `tools/xclient`
through `tests/x/lib.sh`, so they are the same clients the suite uses.

`viewable` and `focus` are read from the X server, so they say what a user sees
rather than what the WM intended. `tags` is the client's `_NET_WM_DESKTOP`, and
prints `-` because the WM publishes no per-client EWMH property yet; the root
properties section says the same about the root window, and it is there because
"did it set `_NET_CLIENT_LIST`" is a question the suite asks.

The pointer starts at the centre of the screen, which is inside one of the
windows. A warp into the window it is already in sends no enter event, so a
focus question needs a move out and back, in that order, inside one `-c`:

```bash
tests/x/probe.sh -n 2 -c 'xdotool mousemove 300 400; sleep 0.3; xdotool mousemove 900 400'
```

The exit status is the command's when one was given and the WM is still
running, and 0 when no command was given. It is 1 when the WM died after it
took the root window: the state section ends with `wm: dead (exit N)` and the
log tail follows. It is also 1 when the WM never became ready, and that path
prints the reason and the log tail on stderr and does not print the table.
That is what makes `-c` usable as an assertion: the answer is the shell's
`$?`, not a line to grep.

It is a tool and not a test. `tests/x/run.sh` discovers `tests/x/test-*.sh`, so
this file never runs as part of the suite; `tests/x/test-102-probe-shape.sh`
pins the output shape, so a column that stops printing is a failed test rather
than a probe nobody trusts.

### Manual session

```bash
Xephyr :2 -screen 1280x800 -noreset &
DISPLAY=:2 ./wm &                     # add 2>&1 | tee /tmp/wm.log for a log
DISPLAY=:2 tools/xclient -n 2 -v      # two bare X11 windows, prints their IDs
```

Look at the Xephyr window. The WM tiles the two windows side by side; with one
master client, the first window takes half the screen and the second fills the
other half. `./wm` is whichever variant was built last, so for a log with
`DEBUG` lines build the debug one first: `make DEBUG=1`, then `DISPLAY=:2 ./wm`.

Kill the WM before you rebuild. `make` replaces `./wm` by renaming a new file
over it, so the build no longer fails while the WM runs, and the WM you started
keeps running the binary it started with: a log that does not show the change
you just made is usually the old process, not a missing event.

Kill the session with `pkill -x Xephyr`, or kill the PIDs you started. Do not
`pkill -f wm`; the pattern matches your own shell command line and kills the
shell.

### Why `tools/xclient` and not `xterm`

An Xephyr server has no usable bitmap fonts, so `xterm` dies with
`cannot load font "-misc-fixed-..."` and `Selected font has no non-zero height`.
`xterm -fa "JetBrains Mono"` fails the same way for the same reason. `tools/xclient`
needs only libxcb: it creates N windows with `WM_CLASS=xclient`,
`WM_NAME=<title>-<n>`, a requested size, and repaints on expose.

```bash
make tools/xclient
```

| Flag | Meaning |
|------|---------|
| `-n N` | number of windows (default 1) |
| `-t TITLE` | title prefix, each window is `TITLE-<n>` |
| `-c CLASS` | `WM_CLASS` name and class (default `xclient`) |
| `-W/-H` | requested width/height (default 400x300) |
| `-s SEC` | sleep between window creations |
| `-l SEC` | exit after SEC seconds instead of waiting to be killed |
| `-v` | print each window ID |
| `-y` | wait for the server to process each map before printing its ID |
| `-o` | create override-redirect windows, which a WM must leave alone |

`xdotool` finds them: `DISPLAY=:2 xdotool search --onlyvisible --class xclient`.

### Inspecting the nested session

```bash
export DISPLAY=:2
xdotool search --onlyvisible --class xclient      # windows the WM actually mapped
xdotool getwindowgeometry --shell <id>            # X=, Y=, WIDTH=, HEIGHT=
xprop -root                                       # EWMH properties the WM set
xprop -id <id> WM_STATE                           # set by a WM on managed windows
xwininfo -id <id>                                 # geometry, border width, map state
xwininfo -id <id> -events                         # the event mask that window selected
xwininfo -root -tree                              # the window tree under the root
xrandr                                            # what the WM should see
xmodmap -pm                                       # which keycode each modifier holds
import -window root /tmp/shot.png                 # screenshot of the nested screen
```

`xdotool search --onlyvisible` filters on `map_state == IsViewable`, so it is
the quickest "did the WM map this" check.

`xwininfo -id <window> -events` prints `Someone wants these events`, `Do not
propagate these events` and `Override redirection?` for that window. The event
mask decides what a focus, pointer or drag handler can see at all: a managed
window that does not select `EnterWindow`/`LeaveWindow` sends `ENTER_NOTIFY` to
the root instead, and the focus handler then sees a crossing that names the root
rather than the client. `xprop` reports properties and not masks, and `xdotool`
has no subcommand for them.

`xmodmap -pm` prints the modifier table: for each of Shift, Lock, Control and
Mod1 through Mod5, the keycodes that carry it. A keybinding masked with a
modifier whose bit no keycode carries never matches, so the keysym looks dead
while the binding is correct. `xdotool key` under `DEBUG=1` prints a keymap
dump: the modifier mask each keysym level requires, not the keycode that sets
the bit.

### Scripted session

```bash
scripts/xephyr-test.sh [--windows N] [--display :N] [--keep] [--verbose]
```

The script picks a free display (`:2`..`:9`), starts Xephyr, starts the WM,
spawns N `xclient` windows, waits 4 s, then checks in order:

1. the WM process is still alive;
2. every test window is viewable;
3. no two windows overlap;
4. window areas cover at least half the screen.

Exit status 0 means all checks passed. Logs go to `$TMPDIR/wm-xephyr-test/` (`wm.log`, `xclient.log`,
`xephyr.log`). `nix develop` sets `TMPDIR` to a per-shell directory, so it is
usually not plain `/tmp`. `--keep` leaves the session running so you can look
at it; the script prints the WM pid and display in that case, and that display
is a private Xvfb nobody can see unless `WM_USE_SESSION_DISPLAY=1` puts it on
yours. The lines that name the parent display, then the variant, the binary and
the log, are described under *Debug build*.

This is written as the acceptance test for the X11 output path. Check 2 passes
since the WM maps windows on `MapRequest`; checks 3 and 4 wait for tiling
(`bd show wm-4f6.3`). Make them pass rather than adjusting the checks.

---

## 5. Performance

```bash
scripts/perf-test.sh [--windows N] [--display :N] [--keep]
```

It starts the same way as `xephyr-test.sh`: it builds the WM variant named by
`DEBUG` and `SANITIZE`, prints the line that names the variant, the binary and
the log, and runs that binary. The baseline below was measured on the release
build; a debug build is `-Og` and its numbers are not comparable with it, which
is why the variant is in the output. It then prints these numbers:

| Number | How it is measured | Why it matters |
|--------|--------------------|----------------|
| idle CPU | `/proc/<wm pid>/stat` utime+stime over 2 s wall (Linux) | the WM must cost nothing when nothing happens; over the script's budget the run fails |
| burst CPU | utime+stime delta while N windows are created | cost of the manage path per window |
| per window | burst CPU / N | comparable across window counts |
| time to tile | wall clock until all N windows are viewable, polled every 10 ms, TIMEOUT at 5 s | the number a user feels |
| RSS | `VmRSS` before/after the burst | leak and growth detection |
| keys while dmenu is open | wall clock from `MOD+2` until the tag-1 windows are hidden, with a `dmenu` that writes nothing | a launcher that waits for dmenu stops the loop, and over the budget the run fails |
| SIGTERM | wall clock from SIGTERM until the WM's `/proc` state is gone or `Z` | a loop that blocks must still be stoppable |

Measured 2026-11-03 (1280x800 Xephyr, release build, 5 windows):

```
idle CPU: 0 ms over 2000 ms wall (budget 20 ms)
time to tile: 15 ms for 5 windows (3.0 ms per window)
burst CPU: 0 ms (0.00 ms per window)
RSS: 2880 kB before, 2880 kB after (0 kB growth)
keys while dmenu is open: MOD+2 reached the WM in 38 ms (budget 500 ms)
SIGTERM: WM exited in 9 ms
```

Idle CPU is a budget, not a baseline: the loop blocks in `poll(2)`, so anything
above a few milliseconds over the window means it is spinning again, and the
script fails the run rather than leaving the number for someone to compare by
eye. `tests/x/test-85-idle-cpu.sh` asserts the same thing inside the seam suite.
The wall clock numbers are the machine's: time to tile is roughly one X round
trip per client on a local socket.

What these numbers do not cover, and what to add when it matters:

- **Per-event service time.** Add a timestamp at the top of
  `xcb_handler_dispatch()` and at the end of `hub_emit()`, keep a ring buffer of
  the last N durations, and expose the p50/p99 over a signal or SIGUSR1 dump.
  Do not print per event: the printing costs more than the event.
- **X round trips.** Every `xcb_*_reply` call is a synchronous round trip. Grep
  for `_reply(` in any hot path. Batching with `xcb_flush` once per loop
  iteration is the rule; `client_show()` and `client_configure()` currently
  flush per call.
- **Long-run growth.** Run `perf-test.sh --windows 200`, watch RSS. For a
  stricter check, run the WM under valgrind (below) and create/destroy windows in
  a loop.

### Memory

```bash
valgrind --leak-check=full --track-origins=yes --log-file=/tmp/vg.log \
  env DISPLAY=:2 ./wm
```

`valgrind` is in the dev shell. For the unit tests instead, name the variant
binary; `make test` builds it and runs it, and needs no root copy to do that:

```bash
make test                                  # builds and runs build/release/test
valgrind --leak-check=full build/release/test
```

AddressSanitizer and UBSan (rebuild with `SANITIZE=1`):

```bash
make test SANITIZE=1             # the whole suite under ASan+UBSan
make SANITIZE=1                  # the instrumented wm binary for a live session
DISPLAY=:2 ./wm
```

`SANITIZE=1` adds `-fsanitize=address,undefined -fno-omit-frame-pointer -g` to
both compile and link. `make SANITIZE=1` alone builds only `wm`, so the test
binary needs `make test SANITIZE=1`. `detect_leaks=1` is on, so a leak makes
the binary exit non-zero with LeakSanitizer's summary even when every assertion
passed. The suite leaves nothing behind at exit, and
`tests/quality/asan-baseline.txt` is empty: a leak that lands later is new debt,
not noise. `scripts/check-sanitize.sh` runs the same binary and fails on a site
the baseline does not list. It also drops `suppressions=` and `log_path=` from
the sanitizer options, checks the binary it ran for the ASan runtime, and fails
when the baseline lists sites and the log holds no report, for the reasons the
nested-server gate below gives.

Remember that `./wm` is now the instrumented binary: run `make` to put the
plain one back. The objects do not need cleaning, see *Build directories*
above.

The unit suite has no X connection, and the code that sends a request returns
before it builds one without a display. To exercise the map, tile and focus
path under the sanitizers there has to be a server:

```bash
scripts/check-sanitize-x.sh     # the WM in Xephyr, ASan and UBSan
```

It fails when the harness fails, when the WM log carries an ASan error, and
when a leak site or UBSan report in that log is missing from
`tests/quality/sanitize-x-baseline.txt`. The harness stops the WM with a signal,
so that log holds whatever was still allocated at exit: known startup sites are
baselined, and the list is meant to shrink. Fixing a baselined site means
updating the baseline in the same commit.

A run that reports nothing is not a clean run until the gate has ruled out the
reasons for it. The harness line must name `variant=release-sanitize`, because
an uninstrumented binary writes a log with no report in it. `suppressions=` and
`log_path=` are dropped from the sanitizer options before the harness starts,
because either one turns a real report into silence. And a baseline with
entries next to a log with no report fails, because that is either the fixes
landing without their baseline update or the sanitizer never running. That
verdict is `scripts/lib/sanitize-report.sh`, shared with the unit-suite gate.

```bash
scripts/check-sanitize-x.sh --update-baseline  # accept today's sites under a live WM
```

### Following the syscalls

```bash
strace -f -e trace=poll,read,write,recvmsg,sendmsg -p "$(pgrep -x wm)"
```

A busy-wait loop shows up as a continuous stream of `poll`/`recvmsg`
returning `EAGAIN`. A loop that blocks is silent until an X event or a signal
arrives, and one `poll` that returns is what wakes it.

---

## 6. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `FATAL: Another window manager is already running` | another WM on that display, usually a stale `wm` from an earlier run | `pgrep -x wm`, kill those PIDs, or pick a different `--display` |
| Xephyr exits at once with `Server is already active for display N` | lock file from a crashed run | `ls /tmp/.X*-lock /tmp/.X11-unix/`, remove the stale pair, or use another display |
| nested screen is black | the WM died before it mapped anything, or this is a stale session | `scripts/xephyr-test.sh --verbose`, read `wm.log` |
| `xterm: cannot load font` | Xephyr has no usable fonts | use `tools/xclient` |
| `xdotool: command not found` | not in the dev shell | `direnv allow` or `nix develop`; if the package the recipe needs is not in `flake.nix` at all, add it to `buildInputs` and commit that with the recipe |
| WM dies silently at startup | `-DDEBUG` build not used, so the reason was never printed | check the variant line of the run, then rerun with `DEBUG=1`, or with `DEBUG=1 scripts/xephyr-test.sh --verbose` |
| no Xephyr window appears anywhere | the run drew into its own private Xvfb, which is the default | read the parent-display line, or add `WM_USE_SESSION_DISPLAY=1` to watch it on your display |
| `xvfb-run and Xvfb are not both on PATH` | not in the dev shell, and the session display is not used as a fallback | `direnv allow` or `nix develop`; `WM_USE_SESSION_DISPLAY=1` is the non-hermetic alternative |
| `scripts/xephyr-test.sh` cannot find `wm` | not built | `make && make tools/xclient` |

---

## 7. What to test when you change something

| Change | Check |
|--------|-------|
| hub, registry, event bus | `make test`, then `make test-standalone` |
| state machine framework | `make test-sm-standalone` |
| any component | its `tests/unit/test-*.c` file under `make test`, plus `scripts/xephyr-test.sh` |
| anything in the manage/map/tile path | `scripts/xephyr-test.sh --windows 2 --verbose` and `--windows 5` |
| event loop, focus, or key handling | `make check-async`, `scripts/perf-test.sh` for idle CPU, plus a manual session to press keys |
| memory-owning code (clients, monitors, SMs) | `valgrind build/release/test` and a `--windows 50` run |

Every change that touches behaviour should leave `make test` green and
`scripts/xephyr-test.sh` at least as green as it was.

