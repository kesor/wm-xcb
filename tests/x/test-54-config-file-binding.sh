#!/usr/bin/env bash
#
# test-54-config-file-binding.sh - the config file decides which keys are bound.
#
# The suite's own WM runs with no config file, so MOD+2 there comes from the
# compiled bindings. This test starts its own display and its own WM, because
# the file is read at startup: a WM already running cannot be handed a different
# one, and the point is that restarting with the file changes what a key does.
#
# Assertions:
#   - a bind line binds MOD+F5 and MOD+F6, keys the compiled configuration does
#     not bind, and they change the view
#   - a compiled binding (MOD+2) does nothing while the file supplies the
#     bindings: the file replaces the compiled table, it does not add to it
#   - the malformed line is reported once, with its line number, and the lines
#     around it still apply
#   - started with no file at all, MOD+2 works again: the compiled bindings are
#     the fallback

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

DIR="$TMPDIR_SEAM/config-file-binding"
rm -rf "$DIR"
mkdir -p "$DIR"
XVFB_LOG="$DIR/xvfb.log"
CONFIG="$DIR/config"
FIRST_LOG="$DIR/wm-with-file.log"
SECOND_LOG="$DIR/wm-without-file.log"

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

CFG_DISPLAY=":$DISPLAY_NUM"
XVFB_PID=""
AWM_PID=""

cleanup() {
  [ -n "$AWM_PID" ] && kill "$AWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$CFG_DISPLAY" -screen 0 1280x800x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

up=0
for _ in $(seq 1 100); do
  if DISPLAY="$CFG_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    up=1
    break
  fi
  sleep 0.05
done
if [ "$up" -ne 1 ]; then
  fail "a private display is up ($CFG_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($CFG_DISPLAY)"

# spawn_client and the check_* helpers work on whatever DISPLAY the test has.
export DISPLAY="$CFG_DISPLAY"

# Line 4 is the one the parser has to reject and report. It sits between lines
# that must still apply.
cat >"$CONFIG" <<'CONFIG_FILE'
# a view of tag 2, on a key the compiled bindings do not have
bind Mod+F5 tag-manager.view 2
bind Mod+F6 tag-manager.view 1
bogus keybinding line
CONFIG_FILE

start_wm() { # start_wm <config-path> <log-file>
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

# press_until <key> <viewable-count> <what> - press <key> until exactly that
# many of this test's windows are viewable. A synthetic key event can be lost,
# and a repeat of a key that already did its work costs one request and changes
# no state.
press_until() { # press_until <key> <count> <what>
  local key=$1 want=$2 what=$3 waited=0

  while [ "$waited" -lt 3000 ]; do
    if [ "$(count_viewable)" = "$want" ]; then
      pass "$what"
      return 0
    fi
    press "$key"
    sleep 0.05
    waited=$((waited + 50))
  done
  fail "$what: $(count_viewable) of 1 windows viewable"
  return 1
}

if ! start_wm "$CONFIG" "$FIRST_LOG"; then
  finish
  exit 1
fi
pass "the WM is running with the config file"

spawn_client 1 400 300 tagged >/dev/null
require_visible 1 "the window is viewable before any key"

press_until super+F5 0 "MOD+F5 from the config file switched to tag 2"
press_until super+F6 1 "MOD+F6 from the config file switched back to tag 1"

# MOD+2 is in the compiled bindings and not in the file. The file replaced the
# table, so the key must do nothing. One synthetic press can be lost, and a
# lost press would pass this check for the wrong reason, so press it several
# times: a binding that is still there hides the window on the first press
# that arrives and leaves it hidden.
for _ in 1 2 3 4 5; do
  press super+2
  sleep 0.05
done
sleep 0.2
check_eq "MOD+2 does nothing while the file supplies the bindings" "1" "$(count_viewable)"

check_eq "the malformed line is reported once, with its line number" "1" \
  "$(grep -cF "ERROR: config $CONFIG:4:" "$FIRST_LOG" 2>/dev/null || true)"
check_eq "the report names the line it could not read" "1" \
  "$(grep -cF "bogus" "$FIRST_LOG" 2>/dev/null || true)"
check_eq "the good bind lines are not reported as errors" "0" \
  "$(grep -cE "ERROR: config $CONFIG:(2|3):" "$FIRST_LOG" 2>/dev/null || true)"

if kill -0 "$AWM_PID" 2>/dev/null; then
  pass "the WM still runs after a malformed line"
else
  fail "the WM died on a malformed line"
fi

# Restart with no file. The compiled bindings have to be back: MOD+2 hides the
# tag-1 window, which is what the suite's own WM does.
kill "$AWM_PID" 2>/dev/null
wait "$AWM_PID" 2>/dev/null
AWM_PID=""

if ! start_wm "$DIR/no-such-config" "$SECOND_LOG"; then
  finish
  exit 1
fi
pass "the WM is running with no config file"

require_visible 1 "the window is still viewable after the restart"
press_until super+2 0 "MOD+2 works again once the file is gone"

finish
