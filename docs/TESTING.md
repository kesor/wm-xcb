# Testing the Window Manager

*Written 2026-11-01. Numbers in this file were measured on this machine.*

> **Bottom line:** `make test` covers the model, `make test-x` covers the seam
> between the WM and the X server, `scripts/perf-test.sh` covers cost. Both start their own nested X
> server, so they need no second machine and no root. Without arguments they
> print PASS/FAIL lines and leave logs under `$TMPDIR` (`wm-xephyr-test/`,
> `wm-perf-test/`).

---

## 0. What runs automatically

`lefthook` runs the gates on every commit and push; CI runs the same commands.
See [QUALITY.md](QUALITY.md). The sections below are the layers you run by hand
while working.

## 1. Environment

The dev shell comes from `flake.nix` via direnv:

```bash
cd ~/src/suckless/wm
direnv allow          # first time, or after editing flake.nix
# alternative without direnv:
nix develop
```

The shell provides gcc, clang, clang-tools, bear, gdb, the libxcb family, and
the test tools: `Xephyr` (from `xorg-server`), `xvfb`, `xdotool`, `xprop`,
`xrandr`, `xterm`, `dmenu`, `imagemagick` (`import` for screenshots), `valgrind`,
and `strace`.

Rule of thumb: anything you run against the WM needs the same shell the WM was
built in, because the binary links the store paths of that shell's libraries.

---

## 2. Unit tests

```bash
make            # normal build, -Os
make test       # builds and runs test/, prints "Total: N passed, M failed"
```

`make test` links every non-`main` object plus the 18 `test-*.c` files and runs
them. Current state: 645 passing assertions, 0 failures. Components are tested
without an X server by calling their handlers with fabricated events and
inspecting hub events; that is the pattern to copy for new components.

Two extra targets:

```bash
make test-sm-standalone    # state machine framework only, prints DEBUG trace
make test-standalone       # BROKEN: link fails, wm-log.o missing from the recipe
```

### Debug build

```bash
make clean && make DEBUG=1
```

Adds `-g3 -O0 -Wall -pedantic -DDEBUG`, which turns `LOG_DEBUG()` into real
output. This is the build to use when you need to see what the WM decided.
`-pedantic` rejects GNU-only constructs; a zero-length array initializer in
`tiling.c` used to break this build and was fixed on 2026-11-01. If the debug
build stops compiling, treat that as a bug: it is the only way to read the
event log.

---

## 3. Seam tests

```bash
make test-x                 # all of tests/x/test-*.sh
make test-x TEST_X_ARGS="--filter=tag"   # one file
make test-x TEST_X_ARGS="--keep"         # leave the session running
make test-x TEST_X_ARGS="--verbose"      # print the WM log for each failure
```

A seam test asserts what the X server sees, not what the WM thinks: the window
is viewable, `WM_STATE` is `Normal`, two windows do not overlap, the input focus
is where the WM said it would be, `_NET_CLIENT_LIST` exists. Those are the
assertions the unit tests cannot make, and `docs/STATUS.md` §4 is entirely made
of things this level would have caught.

`scripts/xephyr-test.sh` is the earlier single-scenario version of the same idea;
prefer `make test-x`, and keep the old script only because several beads issues
name it.

`tests/x/run.sh` starts one Xephyr and one WM for the whole run, then runs each
file as a separate process with `DISPLAY`, `WM_PID`, `WM_LOG` and `ROOT_DIR`
exported. `tests/x/lib.sh` has the helpers: `spawn_client`, `wait_viewable`,
`require_visible`, `rect_of`, `overlap_pairs`, `focused_window`, `press`,
`root_prop`, and the `check_*` assertions.

Current result on this tree: **2 assertions pass, 9 fail**. Every failure names
the defect it belongs to:

| Test | Fails because |
|------|---------------|
| `test-10-map-on-map-request` | the WM never maps the window (4.1) |
| `test-20-wm-state-property` | `WM_STATE` is never set (4.1) |
| `test-30-tiling-two-windows` | windows never become viewable, so tiling is unobservable (4.3) |
| `test-40-focus-on-manage` | no `xcb_set_input_focus` anywhere (4.4) |
| `test-50-tag-switch-keybinding` | no key grab, so `KEY_PRESS` never arrives (4.5) |
| `test-60-ewmh-root-properties` | no root properties are set |
| `test-70-client-exit-cleanup` | blocked on the same mapping failure |
| `test-80-many-windows` | 0 of 20 windows become viewable |

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

Current result: **9 violations, 6 waived**. The violations are the debt listed
in `bd list --parent wm-46k.2`: `wm-xcb.c:216` (the busy-wait),
`wm-xcb-ewmh.c:35` and `:61` (a property-handler path), `monitor-manager.c:468`,
`:499`, `:544` (the RandR handler path), and `launcher.c:216`, `:221`, `:228`
(the launcher that freezes the WM until dmenu exits).

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

Look at the Xephyr window. As of 2026-11-01 the WM does not map the windows, so
the nested screen stays black — that is the defect `docs/STATUS.md` §4.1
describes, not a setup problem.

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
xrandr                                            # what the WM should see
import -window root /tmp/shot.png                 # screenshot of the nested screen
```

`xdotool search --onlyvisible` filters on `map_state == IsViewable`, so it is
the quickest "did the WM map this" check.

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
at it; the script prints the WM pid and display in that case.

This is written as the acceptance test for the X11 output path: it fails today
at check 2, and should be made to pass rather than adjusted.

---

## 5. Performance

```bash
scripts/perf-test.sh [--windows N] [--display :N] [--keep]
```

It starts the same way as `xephyr-test.sh` and prints five numbers:

| Number | How it is measured | Why it matters |
|--------|--------------------|----------------|
| idle CPU | `/proc/<wm pid>/stat` utime+stime over 2 s wall | the WM must cost nothing when nothing happens |
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

`valgrind` is in the dev shell. For the unit tests instead:

```bash
valgrind --leak-check=full ./test
```

AddressSanitizer and UBSan (rebuild with `SANITIZE=1`):

```bash
make clean && make SANITIZE=1
./test                      # 645 assertions, then the leak report
DISPLAY=:2 ./wm             # instrumented live session
```

`SANITIZE=1` adds `-fsanitize=address,undefined -fno-omit-frame-pointer -g` to
both compile and link. Measured 2026-11-01: `./test` passes all assertions and
then reports `936 byte(s) leaked in 47 allocation(s)`, all of it hub registry
bookkeeping (see `docs/STATUS.md` §4.13). `make test` therefore exits non-zero
under `SANITIZE=1`; read the leak summary instead of the exit status.

Remember to `make clean && make` afterwards, or every later run is instrumented.

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
| nested screen is black | expected today: the WM maps nothing | `scripts/xephyr-test.sh --verbose`, read `wm.log` |
| `xterm: cannot load font` | Xephyr has no usable fonts | use `tools/xclient` |
| `xdotool: command not found` | not in the dev shell | `direnv allow` or `nix develop` |
| WM dies silently at startup | `-DDEBUG` build not used, so the reason was never printed | `make DEBUG=1` and rerun |
| `scripts/xephyr-test.sh` cannot find `wm` | not built | `make && make tools/xclient` |

---

## 7. What to test when you change something

| Change | Check |
|--------|-------|
| hub, registry, event bus | `make test`, then `make test-standalone` once it links again |
| state machine framework | `make test-sm-standalone` |
| any component | its `test-*.c` file under `make test`, plus `scripts/xephyr-test.sh` |
| anything in the manage/map/tile path | `scripts/xephyr-test.sh --windows 2 --verbose` and `--windows 5` |
| event loop, focus, or key handling | `make check-async`, `scripts/perf-test.sh` for idle CPU, plus a manual session to press keys |
| memory-owning code (clients, monitors, SMs) | `valgrind ./test` and a `--windows 50` run |

Every change that touches behaviour should leave `make test` green and
`scripts/xephyr-test.sh` at least as green as it was.
