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
`scripts/check-seam.sh`: it starts Xvfb when there is no `DISPLAY`, and fails
rather than skipping when neither is available. See [QUALITY.md](QUALITY.md).
The sections below are the layers you run by hand while working.

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

Each harness run prints one line before it starts anything:

```
xephyr-test.sh: variant=debug (DEBUG=1 SANITIZE=0), binary=<repo>/build/debug/wm, log=<tmpdir>/wm-xephyr-test/wm.log
```

The variant, the binary that runs and the log it writes are in that line, so a
run never leaves you guessing which build it used or where its output went. If
that line says `variant=debug` and the log is still empty, the WM itself never
logged and the binary is not the suspect. `SANITIZE=1` works the same way.

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

`tests/x/run.sh` starts one Xephyr and one WM for the whole run, then runs each
file as a separate process with `DISPLAY`, `WM_PID`, `WM_LOG` and `ROOT_DIR`
exported. `tests/x/lib.sh` has the helpers: `spawn_client`, `wait_viewable`,
`require_visible`, `rect_of`, `overlap_pairs`, `focused_window`, `press`,
`root_prop`, and the `check_*` assertions.

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
| `test-50-tag-switch-keybinding` | the unmap a tag change causes also unmanages the client, so it never comes back |
| `test-60-ewmh-root-properties` | no root properties are set |

Write a test so that it cannot pass for the wrong reason: a precondition such as
"the window is visible before I press the key" uses `require_visible`, which
stops the test, not `check_eq`, which would let a later "the window is gone now"
pass because it was never there.

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
| `xcb_poll_for_event()` | polling instead of blocking burns a core |
| `read()` | a callback must not wait on a child's pipe |
| `waitpid()` without `WNOHANG` | blocking wait on a child |
| `sleep()`, `usleep()`, `nanosleep()` | anything that sleeps stops every client |

A call is waived when it, or the two lines above it, carries
`allow-blocking: <reason>`. The marker is the record of why the wait is safe,
usually startup code that runs before the loop exists. Comments that mention a
blocking function are ignored, so prose cannot produce a false positive.

The script prints the current counts. Every violation is the debt tracked in
`bd list --parent wm-46k.2`: the busy-wait in `handle_xcb_events()`, the
property-handler path through `print_atom_name()` and
`ewmh_check_wm_state_fullscreen()`, the RandR handler path through
`monitor_manager_update_crtc_geometry()` and its two helpers, and the blocking
`read()`/`waitpid()` pair in `launcher_run_dmenu()`.

The check is meant to reach zero by removing the violations, not by adding
markers. A new marker needs a reason that would satisfy a reviewer: "startup,
before the loop runs" is one; "it is probably fine" is not.

---

## 4. Nested X server (Xephyr)

Xephyr renders a second X server inside a window on your real session. The WM
under test runs as the only WM on that second display.

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
at it; the script prints the WM pid and display in that case. The first line
names the variant, the binary and the log, as described under *Debug build*.

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
is why the variant is in the output. It then prints five numbers:

| Number | How it is measured | Why it matters |
|--------|--------------------|----------------|
| idle CPU | `/proc/<wm pid>/stat` utime+stime over 2 s wall (Linux) | the WM must cost nothing when nothing happens |
| burst CPU | utime+stime delta while N windows are created | cost of the manage path per window |
| per window | burst CPU / N | comparable across window counts |
| time to tile | wall clock until all N windows are viewable, polled every 10 ms, TIMEOUT at 5 s | the number a user feels |
| RSS | `VmRSS` before/after the burst | leak and growth detection |

Baseline measured 2026-11-01 (1280x800 Xephyr, 5 and 10 windows):

```
idle CPU: 1990 ms over 2000 ms wall          <- busy-wait loop, see STATUS §4.7
time to tile: TIMEOUT (5000 ms); 0 of 5 viewable
burst CPU: 5010 ms
RSS: 2840 kB before, 2840 kB after
```

So today the number to beat is "100% of a core doing nothing". After the loop is
fixed, idle CPU should be a few milliseconds over 2 s. A reasonable target for
time to tile is under 50 ms for 10 windows, which is roughly one X round trip
per client on a local socket.

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
passed. The suite leaks today at the allocation sites listed in
`tests/quality/asan-baseline.txt`, so a raw run of the whole suite is not the
check: `scripts/check-sanitize.sh` runs the same binary and fails only on a site
that is not in the baseline. It also drops `suppressions=` and `log_path=` from
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

The busy-wait loop shows up as a continuous stream of `poll`/`recvmsg`
returning `EAGAIN`. After the fix, the same trace should be silent until an X
event arrives.

---

## 6. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `FATAL: Another window manager is already running` | another WM on that display, usually a stale `wm` from an earlier run | `pgrep -x wm`, kill those PIDs, or pick a different `--display` |
| Xephyr exits at once with `Server is already active for display N` | lock file from a crashed run | `ls /tmp/.X*-lock /tmp/.X11-unix/`, remove the stale pair, or use another display |
| nested screen is black | the WM died before it mapped anything, or this is a stale session | `scripts/xephyr-test.sh --verbose`, read `wm.log` |
| `xterm: cannot load font` | Xephyr has no usable fonts | use `tools/xclient` |
| `xdotool: command not found` | not in the dev shell | `direnv allow` or `nix develop`; if the package the recipe needs is not in `flake.nix` at all, add it to `buildInputs` and commit that with the recipe |
| WM dies silently at startup | `-DDEBUG` build not used, so the reason was never printed | check the variant on the first line of the run, then rerun with `DEBUG=1`, or with `DEBUG=1 scripts/xephyr-test.sh --verbose` |
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

