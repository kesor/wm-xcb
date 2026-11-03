#!/usr/bin/env bash
#
# test-91-private-parent-display.sh - the seam run nests into a display it owns.
#
# Xephyr draws into a parent display, and this suite used to take the session's
# DISPLAY whenever it was set: a gate run put windows on the desktop it ran on,
# took the input focus with them, and answered differently on a display with
# another depth or a wedged server. run.sh now starts a private Xvfb, prints the
# line naming it and records that line next to the logs; this asserts the run
# really used the display it recorded.
#
# The session's display is not searched for Xephyr windows here: xvfb-run
# replaces XAUTHORITY for the private server, so a test running inside it cannot
# be sure of authenticating to the display the caller was sitting on. What it
# can check is that the recorded parent display is a live server and is not the
# session's.
#
# WM_USE_SESSION_DISPLAY=1 is the documented escape hatch for watching a run by
# eye. It draws on the session on purpose, so that path asserts the opt-out
# instead: a run on the session display that never asked for one is the
# regression this test exists for.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

PARENT="${WM_PARENT_DISPLAY:-}"
KIND="${WM_PARENT_DISPLAY_KIND:-}"
SESSION="${WM_SESSION_DISPLAY:-}"
REPORT="$TMPDIR_SEAM/parent-display.txt"

RECORDED=$(sed -n 's/.*parent display = \([^ ]*\) .*/\1/p' "$REPORT" 2>/dev/null | head -n 1)

check_nonempty "run.sh exports the parent display it used" "$PARENT"
check_nonempty "run.sh records the parent display next to the logs" "$RECORDED"
check_eq "the recorded parent display is the one the run used" "$PARENT" "$RECORDED"
check_eq "the parent display is a live X server" 1 \
  "$(DISPLAY="$PARENT" xdotool getdisplaygeometry >/dev/null 2>&1 && echo 1 || echo 0)"

if [ "${WM_USE_SESSION_DISPLAY:-0}" = "1" ]; then
  check_eq "WM_USE_SESSION_DISPLAY=1 put the run on the session display" session "$KIND"
else
  check_eq "the run used a private parent display" private "$KIND"
  if [ -n "$PARENT" ] && [ "$PARENT" = "$SESSION" ]; then
    fail "the parent display is the session's own ($PARENT); Xephyr is drawing on the desktop"
  else
    pass "the parent display ($PARENT) is not the session display (${SESSION:-unset})"
  fi
fi

finish
