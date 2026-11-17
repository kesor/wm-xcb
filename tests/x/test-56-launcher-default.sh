#!/usr/bin/env bash
#
# test-56-launcher-default.sh - MOD+p on a machine without dmenu says what it
# needs instead of forking a shell that prints "command not found".
#
# The suite puts fake dmenu and dmenu_path programs on the WM's PATH, so a
# missing prompt cannot be produced there. This test starts its own WM with a
# PATH that holds only sh: the pipeline's first exec succeeds, which is what
# makes the shell's own "not found" line the thing a launcher without the
# lookup would print. A PATH with nothing on it fails at exec sh instead and
# the absence assertion below would pass for the wrong reason.
#
# Assertions:
#   - startup prints one line naming dmenu and dmenu_path
#   - pressing MOD+p reports that line once per press and adds no shell's
#     "not found" line
#   - the WM keeps running

set -uo pipefail

source "$(dirname "$0")/lib.sh"

if ! command -v Xvfb >/dev/null 2>&1; then
  fail "Xvfb is available to start a private display"
  finish
  exit 1
fi

VARIANT=$(make -s -C "$ROOT_DIR" print-variant DEBUG="${WM_SEAM_DEBUG:-0}" SANITIZE="${SANITIZE:-0}")
WM_BIN="$ROOT_DIR/build/$VARIANT/wm"
if [ ! -x "$WM_BIN" ]; then
  fail "the WM under test exists ($WM_BIN)"
  finish
  exit 1
fi

DIR="$TMPDIR_SEAM/launcher-default"
rm -rf "$DIR"
mkdir -p "$DIR/bin"
XVFB_LOG="$DIR/xvfb.log"

DISPLAY_NUM=""
for n in 40 41 42 43 44 45 46 47 48 49; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
if [ -z "$DISPLAY_NUM" ]; then
  fail "a free display in :40..:49"
  finish
  exit 1
fi

TEST_DISPLAY=":$DISPLAY_NUM"
XVFB_PID=""
AWM_PID=""

cleanup() {
  [ -n "$AWM_PID" ] && kill "$AWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$TEST_DISPLAY" -screen 0 1280x800x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

up=0
for _ in $(seq 1 100); do
  if DISPLAY="$TEST_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    up=1
    break
  fi
  sleep 0.05
done
if [ "$up" -ne 1 ]; then
  fail "a private display is up ($TEST_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($TEST_DISPLAY)"

export DISPLAY="$TEST_DISPLAY"

# The launcher's child runs `dmenu_path | dmenu` through sh, so a launcher
# without the lookup reaches the shell and prints its line. sh is taken from the
# shell running the test because a Nix machine has no /bin/sh.
SH=$(command -v sh)
if [ -z "$SH" ]; then
  fail "a sh to put on the WM's PATH"
  finish
  exit 1
fi
ln -s "$SH" "$DIR/bin/sh"

LOG="$DIR/wm.log"
WM_CONFIG="$DIR/no-such-config" \
  PATH="$DIR/bin" \
  "$WM_BIN" >"$LOG" 2>&1 &
AWM_PID=$!
sleep 0.5
if ! kill -0 "$AWM_PID" 2>/dev/null; then
  fail "the WM is running after startup"
  tail -n 15 "$LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "the WM is running with a PATH that holds only sh"

check_eq "a machine without dmenu is reported once at startup" "1" \
  "$(grep -c 'no dmenu installed' "$LOG" 2>/dev/null || true)"
check_eq "the report names dmenu and dmenu_path" "1" \
  "$(grep -cE 'no dmenu installed.*dmenu_path' "$LOG" 2>/dev/null || true)"

reported_at_startup=$(grep -c 'no dmenu installed' "$LOG" 2>/dev/null || true)

# Nothing this test installed can answer for dmenu, so a press that still forked
# would leave the shell's own "not found" line in the log.
for _ in 1 2 3; do
  press super+p
  sleep 0.1
done

check_eq "MOD+p adds no shell's 'not found' line" "0" \
  "$(grep -ci 'not found' "$LOG" 2>/dev/null || true)"
check_eq "each MOD+p reports the missing prompt" "3" \
  "$(( $(grep -c 'no dmenu installed' "$LOG" 2>/dev/null || true) - reported_at_startup ))"

if kill -0 "$AWM_PID" 2>/dev/null; then
  pass "the WM still runs after a press with no dmenu installed"
else
  fail "the WM died on a press with no dmenu installed"
fi

finish
