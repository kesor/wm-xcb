#!/usr/bin/env bash
#
# test-23-float-rules.sh - a float rule decides where a window opens.
#
# The rules are the ones the configuration holds: the `float` lines of the file,
# or the compiled ones when the file names none. They match a window's WM_CLASS
# and title, which the WM reads from the server, so this is where the read, the
# match and the placement have to end in the same rectangle the client asked for.
#
# The suite's own WM runs with no config file, so a file needs a WM of its own:
# the file is read at startup.
#
# Assertions:
#   - a window whose class the file's `float` line names keeps the rectangle it
#     asked for, while a window no line names is given the monitor
#   - the floating window keeps that rectangle through a tag round trip, which
#     arranges the monitor twice
#   - started with no file at all, the compiled rule for class `steam` floats
#     that window, and the class the file used in the first half is tiled: the
#     file is gone, so its rule is too

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

DIR="$TMPDIR_SEAM/float-rules"
rm -rf "$DIR"
mkdir -p "$DIR"
XVFB_LOG="$DIR/xvfb.log"
CONFIG="$DIR/config"
WITH_FILE_LOG="$DIR/wm-with-file.log"
NO_FILE_LOG="$DIR/wm-without-file.log"

DISPLAY_NUM=""
for n in 50 51 52 53 54 55 56 57 58 59; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
if [ -z "$DISPLAY_NUM" ]; then
  fail "a free display in :50..:59"
  finish
  exit 1
fi

FR_DISPLAY=":$DISPLAY_NUM"
XVFB_PID=""
AWM_PID=""

cleanup() {
  [ -n "$AWM_PID" ] && kill "$AWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$FR_DISPLAY" -screen 0 1280x800x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

up=0
for _ in $(seq 1 100); do
  if DISPLAY="$FR_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    up=1
    break
  fi
  sleep 0.05
done
if [ "$up" -ne 1 ]; then
  fail "a private display is up ($FR_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($FR_DISPLAY)"

export DISPLAY="$FR_DISPLAY"

# outer_rect <window-id> - prints "x y w h" with the border added, the
# footprint the user sees.
outer_rect() {
  local id=$1 x y w h border
  read -r x y w h <<<"$(rect_of "$id")"
  border=$(border_of "$id")
  printf '%s %s %s %s\n' "$x" "$y" "$((w + 2 * border))" "$((h + 2 * border))"
}

# wait_mapped <window-id> [timeout_ms] - returns 0 once X reports the window as
# viewable. viewable_ids only counts this test's own class, and half of these
# windows have another one.
wait_mapped() {
  local id=$1 timeout=${2:-2000} waited=0
  while [ "$waited" -lt "$timeout" ]; do
    if xwininfo -id "$id" 2>/dev/null | grep -q "Map State: IsViewable"; then
      return 0
    fi
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

require_mapped() { # require_mapped <window-id> <what>
  if wait_mapped "$1"; then
    pass "$2"
  else
    fail "$2: window $1 never became viewable"
    finish
    exit 1
  fi
}

# wait_outer_rect <window-id> <x y w h> [timeout_ms] - returns 0 once the window
# covers exactly that rectangle. A window is viewable before the WM has placed
# it, so a geometry read taken right after the map can see the rectangle the
# client asked for and report it as the layout's.
wait_outer_rect() {
  local id=$1 want=$2 timeout=${3:-2000} waited=0
  while [ "$waited" -lt "$timeout" ]; do
    if [ "$(outer_rect "$id")" = "$want" ]; then
      return 0
    fi
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

require_outer_rect() { # require_outer_rect <window-id> <x y w h> <what>
  if wait_outer_rect "$1" "$2"; then
    pass "$3"
  else
    fail "$3: expected '$2', got '$(outer_rect "$1")'"
  fi
}

# start_wm <config-path> <log-file>
start_wm() {
  local config=$1 log=$2

  WM_CONFIG="$config" "$WM_BIN" >"$log" 2>&1 &
  AWM_PID=$!
  sleep 0.5
  if ! kill -0 "$AWM_PID" 2>/dev/null; then
    fail "the WM is running after startup"
    tail -n 15 "$log" 2>/dev/null | sed 's/^/        /'
    return 1
  fi
  return 0
}

# stop_wm - stop the WM and every client it was managing, so the next half of
# the test starts from an empty screen.
stop_wm() {
  kill_clients
  if [ -n "$AWM_PID" ]; then
    kill "$AWM_PID" 2>/dev/null
    wait "$AWM_PID" 2>/dev/null
    AWM_PID=""
  fi
  sleep 0.2
}

OTHER_CLASS="seam-tiled-$$-$RANDOM"
cat >"$CONFIG" <<CONFIG_FILE
float class=$CLASS
CONFIG_FILE

read -r screen_width screen_height <<<"$(screen_size)"
check_eq "the private display is the size this test assumes" "1280 800" \
  "$screen_width $screen_height"

#
# First half: the file's rule floats the window whose class it names.
#
if ! start_wm "$CONFIG" "$WITH_FILE_LOG"; then
  finish
  exit 1
fi
pass "the WM is running with a float line in its config file"

mapfile -t floated_ids < <(spawn_client 1 400 300 floated)
mapfile -t tiled_ids < <(spawn_client 1 500 350 tiled -c "$OTHER_CLASS")
check_eq "two windows are created with different classes" "1 1" \
  "${#floated_ids[@]} ${#tiled_ids[@]}"

floated=${floated_ids[0]}
tiled=${tiled_ids[0]}

require_mapped "$floated" "the window the rule names is viewable"
require_mapped "$tiled" "the window no rule names is viewable"

# The layout pass is what would take the rectangle away from the window the
# rule names, so the check waits for the pass to have happened - the other
# window is given the monitor - before it reads that window's geometry.
require_outer_rect "$tiled" "0 0 $screen_width $screen_height" \
  "the window no rule names is given the monitor"

# The client asks for 400x300 at 0,0, and its border is one pixel on each side.
check_eq "the window the rule names keeps the rectangle it asked for" \
  "0 0 402 302" "$(outer_rect "$floated")"

# A tag round trip arranges the monitor twice with the floating window out of
# the layout, which is where a rule that only applied once would show.
press super+2
sleep 0.4
if wait_hidden; then
  pass "MOD+2 hid the window the rule names"
else
  fail "MOD+2 did not hide the window the rule names"
fi

press super+1
sleep 0.4
check_eq "MOD+1 brings it back" "1" "$(count_viewable)"
check_eq "the floating window keeps its rectangle across the tag switch" \
  "0 0 402 302" "$(outer_rect "$floated")"
require_outer_rect "$tiled" "0 0 $screen_width $screen_height" \
  "the tiled window takes the monitor again"

stop_wm

#
# Second half: no file, so the compiled rules are what is in force.
#
if ! start_wm "$DIR/no-such-config" "$NO_FILE_LOG"; then
  finish
  exit 1
fi
pass "the WM is running with no config file"

mapfile -t steam_ids < <(spawn_client 1 400 300 steam -c steam)
mapfile -t plain_ids < <(spawn_client 1 500 350 plain -c "$CLASS")
check_eq "two more windows are created" "1 1" "${#steam_ids[@]} ${#plain_ids[@]}"

steam=${steam_ids[0]}
plain=${plain_ids[0]}

require_mapped "$steam" "the class steam window is viewable"
require_mapped "$plain" "the class the file used is viewable"

require_outer_rect "$plain" "0 0 $screen_width $screen_height" \
  "the class the file named is tiled once the file is gone"
check_eq "the compiled rule for class steam floats that window" \
  "0 0 402 302" "$(outer_rect "$steam")"

check_wm_alive
finish
