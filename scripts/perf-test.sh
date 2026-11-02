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
#   idle CPU      WM CPU time consumed over 2 s with no activity
#   burst CPU     WM CPU time consumed while N windows are created
#   per window    burst CPU / N
#   time to tile  wall clock from spawning N windows until all N are viewable
#                 and non-overlapping, or TIMEOUT after 5 s
#   RSS           WM resident memory after the burst
#
# What is not measured, and how to get it later: per-event service time and X
# round trips. Those need counters inside the WM (see docs/TESTING.md).
#
# Exit status: 0 if a measurement ran to completion, 1 if the WM exited or the
#             windows never reached a tiled state (reported as TIMEOUT).

set -uo pipefail

WINDOWS=10
DISPLAY_NUM=""
KEEP=0
SCREEN_W=1280
SCREEN_H=800
TILE_TIMEOUT_MS=5000

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

DISPLAY="$TEST_DISPLAY" "$WM_VARIANT_BIN" >"$WM_LOG" 2>&1 &
WM_PID=$!
sleep 1
if ! kill -0 "$WM_PID" 2>/dev/null; then
  echo "perf-test: WM exited during startup, see $WM_LOG" >&2
  exit 1
fi

echo "nested display: $TEST_DISPLAY  screen: ${SCREEN_W}x${SCREEN_H}  windows: $WINDOWS"

# --- idle CPU -------------------------------------------------------------
cpu0=$(wm_cpu_ms)
sleep 2
cpu1=$(wm_cpu_ms)
echo "idle CPU: $((cpu1 - cpu0)) ms over 2000 ms wall"

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

if [ "$tiled" -ne 1 ]; then
  echo "RESULT: INCOMPLETE - the WM does not map or tile windows, so the tiling number is a timeout"
  exit 1
fi
echo "RESULT: OK"
