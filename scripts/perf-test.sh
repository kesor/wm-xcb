#!/usr/bin/env bash
#
# perf-test.sh - measure what the WM costs while windows come and go.
#
# Usage:
#   scripts/perf-test.sh [--windows N] [--display :N] [--keep]
#
#   --windows N   windows to create in the burst (default 10)
#   --display :N  X display for the nested server (default: first free :2..:9)
#   --keep        leave Xephyr and the WM running on exit
#
# The WM it measures is the variant named by DEBUG and SANITIZE in the
# environment, exactly as `make DEBUG=1` and `make SANITIZE=1` build it, and the
# first line of output names the variant, the binary and the log. A number is
# only worth the build that produced it: a debug build is -O0, so its idle and
# burst CPU are not comparable with the release baseline in docs/TESTING.md.
#
# What is measured (all numbers printed with units):
#   idle CPU      WM CPU time consumed over 2 s with no activity; more than
#                 IDLE_BUDGET_MS means the loop is not blocking, and fails
#   burst CPU     WM CPU time consumed while N windows are created
#   per window    burst CPU / N
#   time to tile  wall clock from spawning N windows until all N are viewable
#                 and non-overlapping, or TIMEOUT after 5 s
#   RSS           WM resident memory after the burst
#   keys          how long MOD+2 takes to hide the tag-1 windows while dmenu is
#                 open; more than KEY_BUDGET_MS means the launcher blocks the
#                 loop, and fails
#   SIGTERM       how long the WM takes to exit after SIGTERM
#
# What is not measured, and how to get it later: per-event service time and X
# round trips. Those need counters inside the WM (see docs/TESTING.md).
#
# Exit status: 0 if a measurement ran to completion, 1 if the WM exited, the
#             windows never reached a tiled state (reported as TIMEOUT), idle
#             CPU went over the budget, a key went unanswered while dmenu was
#             open, or SIGTERM did not stop the WM.

set -uo pipefail

WINDOWS=10
DISPLAY_NUM=""
KEEP=0
SCREEN_W=1280
SCREEN_H=800
TILE_TIMEOUT_MS=5000
# The loop blocks in poll(2) while nothing happens, so a correct WM spends
# milliseconds here. A second or more means it is spinning again.
IDLE_BUDGET_MS=20
IDLE_WINDOW_MS=2000
TERM_TIMEOUT_MS=2000
KEY_BUDGET_MS=500

while [ $# -gt 0 ]; do
  case "$1" in
    --windows)
      WINDOWS="$2"
      shift 2
      ;;
    --display)
      DISPLAY_NUM="${2#:}"
      shift 2
      ;;
    --keep)
      KEEP=1
      shift
      ;;
    -h | --help)
      sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "perf-test: unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
XCLIENT_BIN="$ROOT_DIR/tools/xclient"
LOG_DIR="${TMPDIR:-/tmp}/wm-perf-test"
mkdir -p "$LOG_DIR"
WM_LOG="$LOG_DIR/wm.log"
XEPHYR_LOG="$LOG_DIR/xephyr.log"

# MOD+p runs `dmenu_path | dmenu`. The real dmenu needs fonts a headless nested
# server does not reliably provide; this one writes nothing and stays open,
# which is what the WM sees while a user looks at the prompt.
FAKE_BIN="$LOG_DIR/bin"
mkdir -p "$FAKE_BIN"
cat >"$FAKE_BIN/dmenu" <<FAKE_DMENU
#!/bin/sh
: >"$LOG_DIR/dmenu-started"
sleep 5
FAKE_DMENU
chmod +x "$FAKE_BIN/dmenu"
cat >"$FAKE_BIN/dmenu_path" <<'FAKE_DMENU_PATH'
#!/bin/sh
:
FAKE_DMENU_PATH
chmod +x "$FAKE_BIN/dmenu_path"
# A launcher may exec dmenu_run, as dwm's script does, instead of running dmenu
# itself. Without this name on PATH that launcher exits at once and the keys
# stage below measures an idle WM, which is the opposite of what it is for.
cat >"$FAKE_BIN/dmenu_run" <<'FAKE_DMENU_RUN'
#!/bin/sh
exec dmenu "$@"
FAKE_DMENU_RUN
chmod +x "$FAKE_BIN/dmenu_run"
rm -f "$LOG_DIR/dmenu-started"
PATH="$FAKE_BIN:$PATH"
export PATH

# shellcheck source=lib/wm-variant.sh
. "$ROOT_DIR/scripts/lib/wm-variant.sh"

for cmd in Xephyr xdotool; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "perf-test: missing '$cmd'. Start the dev shell: 'direnv allow' or 'nix develop'." >&2
    exit 1
  fi
done
if [ ! -x "$XCLIENT_BIN" ]; then
  echo "perf-test: build the test client first: make tools/xclient" >&2
  exit 1
fi

wm_build_variant "$WM_LOG" || exit 1

if [ -z "$DISPLAY_NUM" ]; then
  for n in 2 3 4 5 6 7 8 9; do
    if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
      DISPLAY_NUM="$n"
      break
    fi
  done
fi
if [ -z "$DISPLAY_NUM" ]; then
  echo "perf-test: no free X display in :2..:9" >&2
  exit 1
fi
TEST_DISPLAY=":$DISPLAY_NUM"

WM_PID=""
XCLIENT_PID=""
XEPHYR_PID=""

cleanup() {
  if [ "$KEEP" -eq 1 ]; then
    echo "perf-test: keeping session on $TEST_DISPLAY (wm pid $WM_PID)"
    return
  fi
  [ -n "$XCLIENT_PID" ] && kill "$XCLIENT_PID" 2>/dev/null
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$XEPHYR_PID" ] && kill "$XEPHYR_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

now_ms() {
  echo $(($(date +%s%N) / 1000000))
}

CLK_TCK="$(getconf CLK_TCK 2>/dev/null || echo 100)"

# utime+stime of the WM in milliseconds.
wm_cpu_ms() {
  [ -r "/proc/$WM_PID/stat" ] || {
    echo 0
    return
  }
  awk -v hz="$CLK_TCK" '{printf "%d", ($14 + $15) * 1000 / hz}' "/proc/$WM_PID/stat"
}

wm_rss_kb() {
  [ -r "/proc/$WM_PID/status" ] || {
    echo 0
    return
  }
  awk '/^VmRSS:/{print $2}' "/proc/$WM_PID/status"
}

viewable_count() {
  DISPLAY="$TEST_DISPLAY" xdotool search --onlyvisible --class xclient 2>/dev/null | wc -l
}

"$(command -v Xephyr)" "$TEST_DISPLAY" -screen "${SCREEN_W}x${SCREEN_H}" -noreset \
  >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!

for _ in $(seq 1 50); do
  DISPLAY="$TEST_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1 && break
  sleep 0.1
done
if ! DISPLAY="$TEST_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
  echo "perf-test: Xephyr did not start on $TEST_DISPLAY" >&2
  cat "$XEPHYR_LOG" >&2
  exit 1
fi

# The stage presses compiled-in bindings, so a config file in the developer's
# home must not decide what they mean.
WM_CONFIG="$LOG_DIR/no-config-file" DISPLAY="$TEST_DISPLAY" "$WM_VARIANT_BIN" >"$WM_LOG" 2>&1 &
WM_PID=$!
sleep 1
if ! kill -0 "$WM_PID" 2>/dev/null; then
  echo "perf-test: WM exited during startup, see $WM_LOG" >&2
  exit 1
fi
if [ ! -r "/proc/$WM_PID/stat" ]; then
  echo "perf-test: no /proc/$WM_PID/stat, so CPU time cannot be measured on this platform" >&2
  exit 1
fi

echo "nested display: $TEST_DISPLAY  screen: ${SCREEN_W}x${SCREEN_H}  windows: $WINDOWS"

# --- idle CPU -------------------------------------------------------------
cpu0=$(wm_cpu_ms)
sleep 2
cpu1=$(wm_cpu_ms)
idle_cpu=$((cpu1 - cpu0))
echo "idle CPU: ${idle_cpu} ms over ${IDLE_WINDOW_MS} ms wall (budget ${IDLE_BUDGET_MS} ms)"

# --- burst ----------------------------------------------------------------
rss_before=$(wm_rss_kb)
cpu_before=$(wm_cpu_ms)
t_start=$(now_ms)

DISPLAY="$TEST_DISPLAY" "$XCLIENT_BIN" -n "$WINDOWS" -t perfwin -W 400 -H 300 -v \
  >"$LOG_DIR/xclient.log" 2>&1 &
XCLIENT_PID=$!

tiled=0
elapsed=0
while [ "$elapsed" -lt "$TILE_TIMEOUT_MS" ]; do
  if [ "$(viewable_count)" -ge "$WINDOWS" ]; then
    tiled=1
    break
  fi
  if ! kill -0 "$WM_PID" 2>/dev/null; then
    echo "perf-test: WM died during the burst, see $WM_LOG" >&2
    exit 1
  fi
  sleep 0.01
  elapsed=$(($(now_ms) - t_start))
done
t_end=$(now_ms)
cpu_after=$(wm_cpu_ms)
rss_after=$(wm_rss_kb)

burst_cpu=$((cpu_after - cpu_before))
wall=$((t_end - t_start))

if [ "$tiled" -eq 1 ]; then
  echo "time to tile: ${wall} ms for $WINDOWS windows ($(awk -v w="$wall" -v n="$WINDOWS" 'BEGIN{printf "%.1f", w/n}') ms per window)"
elif [ "$wall" -lt "$TILE_TIMEOUT_MS" ]; then
  echo "time to tile: ${wall} ms for $WINDOWS windows ($(awk -v w="$wall" -v n="$WINDOWS" 'BEGIN{printf "%.1f", w/n}') ms per window)"
else
  echo "time to tile: TIMEOUT (${TILE_TIMEOUT_MS} ms); only $(viewable_count) of $WINDOWS windows became viewable"
fi
echo "burst CPU: ${burst_cpu} ms ($(awk -v c="$burst_cpu" -v n="$WINDOWS" 'BEGIN{printf "%.2f", c/n}') ms per window)"
echo "RSS: ${rss_before} kB before, ${rss_after} kB after ($((rss_after - rss_before)) kB growth)"

verdict=0
if [ "$tiled" -ne 1 ]; then
  echo "RESULT: INCOMPLETE - the WM does not map or tile windows, so the tiling number is a timeout"
  verdict=1
fi

# --- keys while dmenu is open ---------------------------------------------
# The promise: the WM never freezes while an application is slow. dmenu is open
# and writing nothing, so a launcher that waits for it never returns to the loop
# and the tag switch arrives late.
DISPLAY="$TEST_DISPLAY" xdotool key --clearmodifiers super+p
dmenu_up=0
for _ in $(seq 1 100); do
  if [ -e "$LOG_DIR/dmenu-started" ]; then
    dmenu_up=1
    break
  fi
  sleep 0.02
done
keys_start=$(now_ms)
keys_answered=0
while [ $(( $(now_ms) - keys_start )) -lt "$KEY_BUDGET_MS" ]; do
  if [ "$(viewable_count)" -eq 0 ]; then
    keys_answered=1
    break
  fi
  DISPLAY="$TEST_DISPLAY" xdotool key --clearmodifiers super+2
  sleep 0.01
done
keys_ms=$(( $(now_ms) - keys_start ))
if [ "$dmenu_up" -ne 1 ]; then
  echo "keys while dmenu is open: MOD+p did not start dmenu, so this line would measure an idle WM"
  verdict=1
elif [ "$keys_answered" -eq 1 ]; then
  echo "keys while dmenu is open: MOD+2 reached the WM in ${keys_ms} ms (budget ${KEY_BUDGET_MS} ms)"
else
  echo "keys while dmenu is open: MOD+2 did not reach the WM within ${KEY_BUDGET_MS} ms"
  verdict=1
fi

# Leave the tag as it was, so the stages after this one measure the same tree.
DISPLAY="$TEST_DISPLAY" xdotool key --clearmodifiers super+1
sleep 0.2

# --- SIGTERM --------------------------------------------------------------
# A loop that blocks stops when the signal arrives, because the handler writes
# the wakeup byte. One that ignores the signal cannot be stopped without
# SIGKILL, which loses the shutdown path and the log.
term_start=$(now_ms)
kill -TERM "$WM_PID" 2>/dev/null
term_exited=0
while [ $(( $(now_ms) - term_start )) -lt "$TERM_TIMEOUT_MS" ]; do
  # A child that has exited is a zombie until it is reaped, so kill -0 keeps
  # succeeding. State Z is what an exited child looks like in /proc.
  state=$(awk '{print $3}' "/proc/$WM_PID/stat" 2>/dev/null)
  if [ -z "$state" ] || [ "$state" = "Z" ]; then
    term_exited=1
    break
  fi
  sleep 0.01
done
term_ms=$(( $(now_ms) - term_start ))
if [ "$term_exited" -eq 1 ]; then
  echo "SIGTERM: WM exited in ${term_ms} ms"
else
  echo "SIGTERM: WM still running ${TERM_TIMEOUT_MS} ms after SIGTERM"
  verdict=1
fi

if [ "$idle_cpu" -gt "$IDLE_BUDGET_MS" ]; then
  echo "RESULT: FAIL - idle CPU ${idle_cpu} ms over ${IDLE_WINDOW_MS} ms wall: the loop does not block"
  exit 1
fi
if [ "$verdict" -ne 0 ]; then
  echo "RESULT: FAIL - see the lines above"
  exit 1
fi
echo "RESULT: OK"
