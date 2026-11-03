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
`xwininfo`, `xrandr`, `xterm`, `dmenu`, `imagemagick` (`import` for screenshots),
`valgrind`, and `strace`.

A tool the harnesses need but the shell lacks is fixed in `flake.nix`, in the
same commit as the recipe that needs it. Each worktree carries its own copy of
`flake.nix`, so the tool appears in that worktree after `direnv allow` or
`nix develop` again, not in the shell that is already running. `scripts/check-flake.sh`
evaluates the shell without entering it, which is what catches a broken
`flake.nix` before the next session finds it; the push gate runs the same check.

Rule of thumb: anything you run against the WM needs the same shell the WM was
built in, because the binary links the store paths of that shell's libraries.

---

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

Adds `-g3 -O0 -Wall -pedantic -DDEBUG`, which turns `LOG_DEBUG()` into real
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

Two files are the exception to "one Xephyr for the whole run" and start their
own display, because the condition they check cannot be produced once the WM is
already up: `test-86-queued-event.sh` and `test-88-adopt-existing-windows.sh`.

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

`run.sh` puts a `dmenu` and a `dmenu_path` of its own at the front of `PATH`.
The fake dmenu records its start in `$WM_SEAM_DMENU_STARTED` and then sleeps,
writing nothing; the fake list source prints nothing. A real dmenu needs fonts
the nested server may not have, and a test that presses `MOD+p` wants a child
that holds the pipe open, not a menu. `test-92-launcher-does-not-block.sh` uses
it: it waits for the marker so an unstarted launcher cannot make the assertion
pass on an idle WM, then requires `MOD+2` to hide the tag-1 window within 500 ms.
`scripts/perf-test.sh` runs the same stage and fails the run when the key goes
unanswered. The fake does not grab the keyboard; a real dmenu does, so `MOD+2`
would not reach the WM while a real prompt is up. What the stage measures is
that the WM's loop still answers, which is the promise; the fake is the only
dmenu that lets the test see it.

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
| `xcb_*_reply()`, `xcb_wait_for_reply()`, `xcb_request_check()` | synchronous round trip: the loop waits for the server |
| `read()` | a callback must not wait on a child's pipe |
| `waitpid()` without `WNOHANG` | blocking wait on a child |
| `sleep()`, `usleep()`, `nanosleep()` | anything that sleeps stops every client |

`xcb_poll_for_event()` is not on the list. It returns NULL instead of waiting,
which is what the loop does to drain the events libxcb queued after `poll(2)`
reported the connection readable, and a grep cannot tell that from the spin it
replaced. Busy-waiting is measured instead: `tests/x/test-85-idle-cpu.sh` in the
seam suite, and the idle CPU line of `scripts/perf-test.sh`.

A call is waived when it, or the two lines above it, carries
`allow-blocking: <reason>`. The marker is the record of why the wait is safe:
startup code that runs before the loop exists, or the non-blocking read of the
signal wakeup pipe in `signals_drain_wakeups()`. Comments that mention a
blocking function are ignored, so prose cannot produce a false positive.

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
`tests/x/run.sh`, `scripts/check-seam.sh` and `scripts/check-sanitize-x.sh` get
a private Xvfb for it, started by the run itself, whether or not `DISPLAY` is
set: no Xephyr window appears on the desktop you are working on, and the run
answers the same on a display with another depth or a wedged server. The rule
lives in `scripts/lib/private-display.sh`, and each run prints the display it
used:

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
build; a debug build is `-O0` and its numbers are not comparable with it, which
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

