#!/usr/bin/env bash
#
# xephyr-test.sh - run the WM in a nested X server and check what it does to
# the windows it is supposed to manage.
#
# Usage:
#   scripts/xephyr-test.sh [--windows N] [--display :N] [--keep] [--verbose]
#
#   --windows N   number of test windows to spawn (default 2)
#   --display :N  X display for the nested server (default: first free :2..:9)
#   --keep        leave Xephyr, the WM, and the test windows running on exit
#   --verbose     echo the WM log and every window geometry
#
# The WM it runs is the variant named by DEBUG and SANITIZE in the environment,
# exactly as `make DEBUG=1` and `make SANITIZE=1` build it:
#   DEBUG=1 scripts/xephyr-test.sh --verbose
# The first two lines name the parent display it draws into and then the
# variant, the binary and the log, so a run never leaves you guessing which
# binary it was, where its log went, or which display it used.
#
# The parent display Xephyr draws into is WM_PARENT_DISPLAY: a private Xvfb
# this run starts, or the one an outer harness already started. See
# scripts/lib/private-display.sh. --keep leaves the session on that display,
# which nobody can see unless it is the session's, so looking at a run by eye
# means both:
#   WM_USE_SESSION_DISPLAY=1 scripts/xephyr-test.sh --keep
#
# Checks, in order:
#   1. The WM stays alive for the whole run.
#   2. Every test window ends up viewable (mapped by the WM).
#   3. Test windows are tiled: they cover the screen and do not overlap.
#
# Exit status: 0 if all checks pass, 1 otherwise.
#
# This is the acceptance test for the X11 output path: it turns green when the
# WM maps windows and tiles them on the monitor.

set -uo pipefail

# The parent-display helper re-executes this script, so it needs the arguments
# from before the loop below consumed them.
SCRIPT_ARGS=("$@")

WINDOWS=2
DISPLAY_NUM=""
KEEP=0
VERBOSE=0
SCREEN_W=1280
SCREEN_H=800
RUN_SECONDS=4

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
    --verbose)
      VERBOSE=1
      shift
      ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *)
      echo "xephyr-test: unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
XCLIENT_BIN="$ROOT_DIR/tools/xclient"
LOG_DIR="${TMPDIR:-/tmp}/wm-xephyr-test"
mkdir -p "$LOG_DIR"
WM_LOG="$LOG_DIR/wm.log"
XCLIENT_LOG="$LOG_DIR/xclient.log"
XEPHYR_LOG="$LOG_DIR/xephyr.log"

# shellcheck source=lib/wm-variant.sh
. "$ROOT_DIR/scripts/lib/wm-variant.sh"
# shellcheck source=lib/private-display.sh
. "$ROOT_DIR/scripts/lib/private-display.sh"

fail() {
  echo "FAIL: $*" >&2
  FAILED=1
}
FAILED=0

need() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "xephyr-test: missing required command '$1'." >&2
    echo "Start the dev shell first: 'direnv allow' or 'nix develop'." >&2
    exit 1
  fi
}
need Xephyr
need xdotool
need xprop

# Before the build: the helper re-executes this script, and a build run twice
# prints the variant line twice, which reads as two runs in the log.
wm_parent_display "${SCRIPT_ARGS[@]}" || exit 1

# Build first, and say which build it is, before Xephyr starts. These scripts
# start the WM themselves, and nothing else rebuilds it, so a stale binary
# reports the behaviour of the tree as it was and the result reads as a WM
# defect.
wm_build_variant "$WM_LOG" || exit 1

pick_display() {
  local n
  for n in 2 3 4 5 6 7 8 9; do
    # wm_display_free clears a lock whose server is gone and says so, instead of
    # skipping that display for every run from then on.
    if wm_display_free "$n"; then
      echo "$n"
      return 0
    fi
  done
  echo ""
}

if [ -z "$DISPLAY_NUM" ]; then
  DISPLAY_NUM="$(pick_display)"
fi
if [ -z "$DISPLAY_NUM" ]; then
  echo "xephyr-test: no free X display in :2..:9" >&2
  exit 1
fi
TEST_DISPLAY=":$DISPLAY_NUM"

if [ ! -x "$WM_VARIANT_BIN" ]; then
  echo "xephyr-test: $WM_VARIANT_BIN missing, run 'make' first" >&2
  exit 1
fi
if [ ! -x "$XCLIENT_BIN" ]; then
  echo "xephyr-test: $XCLIENT_BIN missing, run 'make tools/xclient' first" >&2
  exit 1
fi

WM_PID=""
XCLIENT_PID=""
XEPHYR_PID=""

cleanup() {
  if [ "$KEEP" -eq 1 ]; then
    echo "xephyr-test: keeping the nested session on $TEST_DISPLAY (wm pid $WM_PID)"
    if [ "${WM_PARENT_DISPLAY_KIND:-private}" = "private" ]; then
      # The parent Xvfb is started by xvfb-run, which kills it when this process
      # exits, so the nested server dies with it. Saying "keeping session" without
      # that was a promise the next command disproved.
      echo "xephyr-test: the parent display $WM_PARENT_DISPLAY is a private Xvfb this run started, and it goes away with the run." >&2
      echo "xephyr-test: to look at a kept session, use the session's display: WM_USE_SESSION_DISPLAY=1 scripts/xephyr-test.sh --keep" >&2
    fi
    return
  fi
  [ -n "$XCLIENT_PID" ] && kill "$XCLIENT_PID" 2>/dev/null
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$XEPHYR_PID" ] && kill "$XEPHYR_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

wait_for_display() {
  local i
  for i in $(seq 1 50); do
    if DISPLAY="$TEST_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

# DISPLAY may be a nested server a caller already started. The parent to draw
# into is the one the helper named.
DISPLAY="$WM_PARENT_DISPLAY" "$(command -v Xephyr)" "$TEST_DISPLAY" -screen "${SCREEN_W}x${SCREEN_H}" -noreset \
  >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!

if ! wait_for_display; then
  echo "xephyr-test: Xephyr did not come up on $TEST_DISPLAY" >&2
  cat "$XEPHYR_LOG" >&2
  exit 1
fi

# This harness does not press keys, but it starts the same WM the seam suite
# does. A config file in the developer's home must not decide which bindings
# or float rules that WM loaded. The path is one that does not exist.
WM_CONFIG="$LOG_DIR/no-config-file" DISPLAY="$TEST_DISPLAY" "$WM_VARIANT_BIN" >"$WM_LOG" 2>&1 &
WM_PID=$!
sleep 1

if ! kill -0 "$WM_PID" 2>/dev/null; then
  echo "FAIL: WM exited immediately. Log:" >&2
  cat "$WM_LOG" >&2
  exit 1
fi
echo "PASS: WM running (pid $WM_PID) on $TEST_DISPLAY"

DISPLAY="$TEST_DISPLAY" "$XCLIENT_BIN" -n "$WINDOWS" -t testwin -W 400 -H 300 -v \
  >"$XCLIENT_LOG" 2>&1 &
XCLIENT_PID=$!
sleep "$RUN_SECONDS"

if ! kill -0 "$WM_PID" 2>/dev/null; then
  fail "WM died while windows were being created (see $WM_LOG)"
fi

mapfile -t WINDOW_IDS < <(DISPLAY="$TEST_DISPLAY" xdotool search --onlyvisible --class xclient 2>/dev/null)
MAPPED=${#WINDOW_IDS[@]}
echo "windows requested: $WINDOWS, viewable: $MAPPED"
if [ "$MAPPED" -lt "$WINDOWS" ]; then
  fail "$((WINDOWS - MAPPED)) of $WINDOWS windows were never mapped; the WM does not map windows on MapRequest"
fi

if [ "$MAPPED" -gt 0 ]; then
  # xdotool --shell prints X=.., Y=.., WIDTH=.., HEIGHT=..; collapse to one line.
  rect_of() {
    DISPLAY="$TEST_DISPLAY" xdotool getwindowgeometry --shell "$1" 2>/dev/null \
      | awk -F= '/^X=/{x=$2}/^Y=/{y=$2}/^WIDTH=/{w=$2}/^HEIGHT=/{h=$2}END{print x+0, y+0, w+0, h+0}'
  }

  declare -a RGX RGY RGW RGH
  area_total=0
  for i in "${!WINDOW_IDS[@]}"; do
    read -r rx ry rw rh <<<"$(rect_of "${WINDOW_IDS[$i]}")"
    RGX[i]=$rx
    RGY[i]=$ry
    RGW[i]=$rw
    RGH[i]=$rh
    area_total=$((area_total + rw * rh))
    if [ "$VERBOSE" -eq 1 ]; then
      echo "  ${WINDOW_IDS[$i]}: $rx,$ry ${rw}x${rh}"
    fi
  done

  overlaps=0
  for i in "${!WINDOW_IDS[@]}"; do
    for j in "${!WINDOW_IDS[@]}"; do
      [ "$j" -le "$i" ] && continue
      ax1=${RGX[$i]}
      ay1=${RGY[$i]}
      ax2=$((ax1 + RGW[$i]))
      ay2=$((ay1 + RGH[$i]))
      bx1=${RGX[$j]}
      by1=${RGY[$j]}
      bx2=$((bx1 + RGW[$j]))
      by2=$((by1 + RGH[$j]))
      if [ "$ax1" -lt "$bx2" ] && [ "$bx1" -lt "$ax2" ] && [ "$ay1" -lt "$by2" ] && [ "$by1" -lt "$ay2" ]; then
        overlaps=$((overlaps + 1))
      fi
    done
  done
  if [ "$overlaps" -gt 0 ]; then
    fail "$overlaps window pair(s) overlap; tiling is not being applied"
  else
    echo "PASS: no windows overlap"
  fi

  screen_area=$((SCREEN_W * SCREEN_H))
  if [ "$area_total" -lt $((screen_area / 2)) ]; then
    fail "windows cover $area_total px of $screen_area; expected tiled windows to fill most of the screen"
  else
    echo "PASS: windows cover $area_total px of $screen_area"
  fi

  if [ "$VERBOSE" -eq 1 ]; then
    echo "--- root window properties ---"
    DISPLAY="$TEST_DISPLAY" xprop -root
    echo "--- WM log tail ---"
    tail -n 40 "$WM_LOG"
  fi
fi

if [ "$FAILED" -ne 0 ]; then
  echo "RESULT: FAIL (logs in $LOG_DIR)"
  exit 1
fi
echo "RESULT: PASS"
