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
# Checks, in order:
#   1. The WM stays alive for the whole run.
#   2. Every test window ends up viewable (mapped by the WM).
#   3. Test windows are tiled: they cover the screen and do not overlap.
#
# Exit status: 0 if all checks pass, 1 otherwise.
#
# The WM does not map or tile windows yet (bd show wm-4f6.1),
# so this script currently fails check 2. It is written as the acceptance test
# for the X11 output path: it turns green when that path works.

set -uo pipefail

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
      sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "xephyr-test: unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WM_BIN="$ROOT_DIR/wm"
XCLIENT_BIN="$ROOT_DIR/tools/xclient"
LOG_DIR="${TMPDIR:-/tmp}/wm-xephyr-test"
mkdir -p "$LOG_DIR"
WM_LOG="$LOG_DIR/wm.log"
XCLIENT_LOG="$LOG_DIR/xclient.log"
XEPHYR_LOG="$LOG_DIR/xephyr.log"

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

pick_display() {
  local n
  for n in 2 3 4 5 6 7 8 9; do
    if [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ]; then
      continue
    fi
    echo "$n"
    return 0
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

if [ ! -x "$WM_BIN" ]; then
  echo "xephyr-test: $WM_BIN missing, run 'make' first" >&2
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
    echo "xephyr-test: keeping session on $TEST_DISPLAY (wm pid $WM_PID)"
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

"$(command -v Xephyr)" "$TEST_DISPLAY" -screen "${SCREEN_W}x${SCREEN_H}" -noreset \
  >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!

if ! wait_for_display; then
  echo "xephyr-test: Xephyr did not come up on $TEST_DISPLAY" >&2
  cat "$XEPHYR_LOG" >&2
  exit 1
fi

# stdbuf keeps DEBUG lines in the log when the WM is built with DEBUG=1.
DISPLAY="$TEST_DISPLAY" stdbuf -oL -eL "$WM_BIN" >"$WM_LOG" 2>&1 &
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
