#!/usr/bin/env bash
#
# run.sh - run the seam tests against a live WM in a nested X server.
#
# Usage:
#   tests/x/run.sh [--filter PATTERN] [--keep] [--verbose]
#
#   --filter PATTERN  only run test files whose name contains PATTERN
#   --keep            leave Xephyr and the WM running afterwards
#   --verbose         echo the WM log after a failed test
#
# Each test file is tests/x/test-*.sh. It gets DISPLAY, WM_PID, WM_LOG and
# ROOT_DIR in its environment, sources tests/x/lib.sh, and exits 0 only if all
# of its assertions passed.
#
# Exit status: 0 if every test passed, 1 otherwise.
#
# The suite asserts what a user sees on the X server. Assertions still fail
# where tiling, key grabs, focus and the EWMH root properties are missing
# (bd show wm-4f6); those failures are the specification for the X11 output
# path work.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export ROOT_DIR

FILTER=""
KEEP=0
VERBOSE=0
SCREEN_W=1280
SCREEN_H=800

while [ $# -gt 0 ]; do
  case "$1" in
    --filter)
      FILTER="$2"
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
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "run.sh: unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done

for cmd in Xephyr xdotool xprop; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "run.sh: missing '$cmd'. Start the dev shell: direnv allow" >&2
    exit 1
  }
done

[ -x "$ROOT_DIR/wm" ] || {
  echo "run.sh: build the WM first: make" >&2
  exit 1
}
[ -x "$ROOT_DIR/tools/xclient" ] || {
  echo "run.sh: build the test client first: make tools/xclient" >&2
  exit 1
}

# Xephyr draws into the session the caller is sitting in, so keep the host
# display: overriding DISPLAY below is what the nested server must NOT inherit.
HOST_DISPLAY="${DISPLAY:-}"
[ -n "$HOST_DISPLAY" ] || {
  echo "run.sh: DISPLAY is unset; Xephyr has no host X server to draw into" >&2
  exit 1
}

DISPLAY_NUM=""
for n in 2 3 4 5 6 7 8 9; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
[ -n "$DISPLAY_NUM" ] || {
  echo "run.sh: no free X display in :2..:9" >&2
  exit 1
}
export DISPLAY=":$DISPLAY_NUM"

LOG_DIR="${TMPDIR:-/tmp}/wm-seam-tests"
mkdir -p "$LOG_DIR"
export WM_LOG="$LOG_DIR/wm.log"
export TMPDIR_SEAM="$LOG_DIR"

XEPHYR_LOG="$LOG_DIR/xephyr.log"
XEPHYR_PID=""
WM_PID=""

cleanup() {
  if [ "$KEEP" -eq 1 ]; then
    echo "run.sh: keeping session on $DISPLAY (wm pid $WM_PID)"
    return
  fi
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$XEPHYR_PID" ] && kill "$XEPHYR_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

DISPLAY="$HOST_DISPLAY" Xephyr "$DISPLAY" -screen "${SCREEN_W}x${SCREEN_H}" -noreset \
  >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!

for _ in $(seq 1 50); do
  xdotool getdisplaygeometry >/dev/null 2>&1 && break
  sleep 0.1
done
xdotool getdisplaygeometry >/dev/null 2>&1 || {
  echo "run.sh: Xephyr did not start on $DISPLAY" >&2
  cat "$XEPHYR_LOG" >&2
  exit 1
}

# stdbuf keeps LOG_DEBUG lines in the file when the WM is built with DEBUG=1.
stdbuf -oL -eL "$ROOT_DIR/wm" >"$WM_LOG" 2>&1 &
WM_PID=$!
sleep 1
kill -0 "$WM_PID" 2>/dev/null || {
  echo "run.sh: WM exited during startup:" >&2
  cat "$WM_LOG" >&2
  exit 1
}
export WM_PID

TESTS=()
for f in "$ROOT_DIR"/tests/x/test-*.sh; do
  [ -e "$f" ] || continue
  if [ -n "$FILTER" ] && [[ "$f" != *"$FILTER"* ]]; then
    continue
  fi
  TESTS+=("$f")
done

echo "seam tests on $DISPLAY, ${#TESTS[@]} file(s)"
TOTAL_PASS=0
TOTAL_FAIL=0
FAILED_FILES=()

for f in "${TESTS[@]}"; do
  name=$(basename "$f")
  printf '\n%s\n' "$name"
  out=$(bash "$f" 2>&1)
  rc=$?
  printf '%s\n' "$out"
  pass_here=$(printf '%s\n' "$out" | grep -c '^  ok ')
  fail_here=$(printf '%s\n' "$out" | grep -c '^  FAIL ')
  TOTAL_PASS=$((TOTAL_PASS + pass_here))
  TOTAL_FAIL=$((TOTAL_FAIL + fail_here))
  if [ "$rc" -ne 0 ]; then
    FAILED_FILES+=("$name")
    if [ "$VERBOSE" -eq 1 ]; then
      echo "  --- WM log tail ---"
      tail -n 25 "$WM_LOG" | sed 's/^/        /'
    fi
  fi
  # A test that killed the WM stops the run: every later test would report the
  # same failure.
  if ! kill -0 "$WM_PID" 2>/dev/null; then
    echo "  WM died during $name; stopping the run"
    TOTAL_FAIL=$((TOTAL_FAIL + 1))
    break
  fi
done

echo
echo "seam tests: $TOTAL_PASS passed, $TOTAL_FAIL failed, logs in $LOG_DIR"
if [ "${#FAILED_FILES[@]}" -gt 0 ]; then
  echo "failed: ${FAILED_FILES[*]}"
  exit 1
fi
