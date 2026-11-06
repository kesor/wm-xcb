#!/usr/bin/env bash
#
# test-56-terminal-default.sh - MOD+Return runs a terminal this machine has,
# and a machine without one says so once.
#
# The suite's own WM inherits whatever terminals the machine running the suite
# has installed, so pressing MOD+Return there starts a real terminal on a
# display nobody is looking at. This test starts its own WM instead, with a PATH
# that holds only the terminals it installs, so the terminal the WM picks is the
# terminal the test asked for.
#
# Assertions:
#   - started with no config file and a terminal installed, the WM prints no
#     ERROR line
#   - MOD+Return starts x-terminal-emulator when it is installed, not the xterm
#     that is also there: the distribution's choice wins over a terminal that
#     is merely present
#   - with x-terminal-emulator gone, MOD+Return falls through to xterm
#   - with no terminal installed, startup prints one message naming what it
#     tried, and pressing MOD+Return does not add the shell's own "not found"
#     line on top of it

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

DIR="$TMPDIR_SEAM/terminal-default"
rm -rf "$DIR"
mkdir -p "$DIR/bin" "$DIR/started"
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

# The WM's child runs the terminal through `sh -c`, so sh has to be on the PATH
# this test hands the WM. It is taken from the shell running the test rather
# than named as /bin/sh, which a Nix machine does not have, and only that
# directory goes in: the dev shell's PATH holds xterm and x-terminal-emulator,
# and a WM that fell back to one of those would pass the checks below without
# running anything this test installed.
SH_DIR=$(dirname "$(command -v sh)")
if [ ! -x "$SH_DIR/sh" ]; then
  fail "a sh to put on the WM's PATH ($SH_DIR/sh)"
  finish
  exit 1
fi

# install_terminal <program> - an executable under that name which records that
# it ran, so a terminal the WM started is told from one it only resolved.
#
# The record is a line, not an empty file. `: >file` creates a file whose
# line count is 0, the same count a program that never ran produces, so a
# check that xterm did not start could not fail.
install_terminal() { # install_terminal <program>
  cat >"$DIR/bin/$1" <<FAKE
#!/bin/sh
printf 'ran\n' >>"\${WM_TERMINAL_MARKER_DIR:?}/$1"
exit 0
FAKE
  chmod 0755 "$DIR/bin/$1"
}

start_wm() { # start_wm <bin-dir> <log-file>
  local bin=$1 log=$2

  WM_CONFIG="$DIR/no-such-config" \
    WM_TERMINAL_MARKER_DIR="$DIR/started" \
    PATH="$bin:$SH_DIR" \
    "$WM_BIN" >"$log" 2>&1 &
  AWM_PID=$!
  sleep 0.5
  if ! kill -0 "$AWM_PID" 2>/dev/null; then
    fail "the WM is running after startup"
    tail -n 15 "$log" 2>/dev/null | sed 's/^/        /'
    return 1
  fi
  return 0
}

stop_wm() {
  [ -n "$AWM_PID" ] || return 0
  kill "$AWM_PID" 2>/dev/null
  wait "$AWM_PID" 2>/dev/null
  AWM_PID=""
}

# press_until <marker> <what> - press MOD+Return until the terminal the WM
# picked records that it ran. A synthetic key event can be lost, and a second
# press starts a second terminal at worst.
press_until_started() { # press_until_started <marker> <what>
  local marker=$1 what=$2

  rm -f "$DIR/started/$marker"
  for _ in $(seq 1 100); do
    if [ -e "$DIR/started/$marker" ]; then
      pass "$what"
      return 0
    fi
    press super+Return
    sleep 0.05
  done
  fail "$what: $DIR/started/$marker never appeared"
  return 1
}

count_started() { # count_started <program>
  output_lines "$DIR/started/$1"
}

# First: both terminals installed. x-terminal-emulator is the distribution's
# answer to "which terminal does this machine want", and it must win.
install_terminal xterm
install_terminal x-terminal-emulator

LOG1="$DIR/wm-both.log"
if ! start_wm "$DIR/bin" "$LOG1"; then
  finish
  exit 1
fi
pass "the WM is running with no config file and a terminal installed"

check_eq "a start with no config file prints no ERROR" "0" \
  "$(grep -c '^ERROR' "$LOG1" 2>/dev/null || true)"

press_until_started x-terminal-emulator "MOD+Return starts x-terminal-emulator"
check_eq "MOD+Return does not fall back to xterm while x-terminal-emulator is installed" "0" \
  "$(count_started xterm)"

stop_wm

# Second: only xterm installed. A machine where the distribution's choice is
# missing still gets a terminal.
rm -f "$DIR/bin/x-terminal-emulator" "$DIR/started/xterm"
LOG2="$DIR/wm-xterm-only.log"
if ! start_wm "$DIR/bin" "$LOG2"; then
  finish
  exit 1
fi
pass "the WM is running with xterm as the only terminal"

press_until_started xterm "MOD+Return falls through to xterm when x-terminal-emulator is missing"

stop_wm

# Third: no terminal at all. One message naming what was tried, at startup, and
# a press that reports rather than forking a shell.
EMPTY_DIR="$DIR/empty-bin"
mkdir -p "$EMPTY_DIR"
rm -f "$DIR/started"/*
LOG3="$DIR/wm-none.log"
if ! start_wm "$EMPTY_DIR" "$LOG3"; then
  finish
  exit 1
fi
pass "the WM is running with no terminal installed"

check_eq "a machine with no terminal is reported once at startup" "1" \
  "$(grep -c 'no terminal emulator installed' "$LOG3" 2>/dev/null || true)"
check_eq "the report names the terminal names that were tried" "1" \
  "$(grep -c 'no terminal emulator installed.*x-terminal-emulator.*kitty.*xterm' "$LOG3" 2>/dev/null || true)"
check_eq "the report names \$TERMINAL" "1" \
  "$(grep -cF "tried \$TERMINAL" "$LOG3" 2>/dev/null || true)"

# Nothing this test installed is reachable, so a press that still forked would
# leave the shell's own "not found" line in the log.
for _ in 1 2 3; do
  press super+Return
  sleep 0.1
done
check_eq "MOD+Return adds no shell error to the one report" "0" \
  "$(grep -ci 'not found' "$LOG3" 2>/dev/null || true)"
check_eq "every ERROR in the log is the report, with nothing after it" "0" \
  "$(grep '^ERROR' "$LOG3" 2>/dev/null | grep -vc 'no terminal emulator installed' || true)"

if kill -0 "$AWM_PID" 2>/dev/null; then
  pass "the WM still runs after a press with no terminal installed"
else
  fail "the WM died on a press with no terminal installed"
fi

finish
