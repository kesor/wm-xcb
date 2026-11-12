#!/usr/bin/env bash
#
# test-58-bind-table-capacity.sh - a file may name more bindings than the
# compiled set wires, and every line it names is bound.
#
# The compiled table has no room to spare for the first binding a user adds, so
# the only way to know that a file's bindings all land is to run one: the file
# replaces the compiled bindings, and a registration the table refuses is a key
# that silently does nothing. This test starts its own display and its own WM
# because the file is read at startup.
#
# Assertions:
#   - 40 bind lines, more than the compiled set wires, start the WM with no
#     ERROR line at all, so no line was refused
#   - the first line and the fortieth line each change the tag view, so the
#     table holds both ends of the file and not just its beginning
#   - a reload whose line 41 repeats line 1's chord is reported against line 41,
#     with the cause that differs from a full table: the chord is already bound,
#     and the message names the action it is bound to

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

DIR="$TMPDIR_SEAM/bind-table-capacity"
rm -rf "$DIR"
mkdir -p "$DIR"
XVFB_LOG="$DIR/xvfb.log"
CONFIG="$DIR/config"
WM_LOG_FILE="$DIR/wm.log"

DISPLAY_NUM=""
for n in 60 61 62 63 64 65 66 67 68 69; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
if [ -z "$DISPLAY_NUM" ]; then
  fail "a free display in :60..:69"
  finish
  exit 1
fi

CAP_DISPLAY=":$DISPLAY_NUM"
XVFB_PID=""
AWM_PID=""

cleanup() {
  [ -n "$AWM_PID" ] && kill "$AWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$CAP_DISPLAY" -screen 0 1280x800x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

up=0
for _ in $(seq 1 100); do
  if DISPLAY="$CAP_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    up=1
    break
  fi
  sleep 0.05
done
if [ "$up" -ne 1 ]; then
  fail "a private display is up ($CAP_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($CAP_DISPLAY)"

export DISPLAY="$CAP_DISPLAY"

# write_config [repeated-line] - 40 bind lines on four modifier sets of F1 to F12,
# plus one more line when the caller passes it. Line 1 shows tag 1 and line 40
# shows tag 2, so either end of the file is a chord this test can press. The
# lines between them bind the same action: the point is the count, not what the
# keys do.
write_config() {
  local repeated=${1:-} modifiers key i

  for i in $(seq 0 39); do
    case $((i / 12)) in
      0) modifiers=Mod ;;
      1) modifiers=Mod+Shift ;;
      2) modifiers=Ctrl+Mod ;;
      3) modifiers=Mod+Alt ;;
    esac
    key=$((i % 12 + 1))
    if [ "$i" -eq 0 ]; then
      printf 'bind %s+F%s tag-manager.view 1\n' "$modifiers" "$key" >>"$CONFIG"
    else
      printf 'bind %s+F%s tag-manager.view 2\n' "$modifiers" "$key" >>"$CONFIG"
    fi
  done
  [ -n "$repeated" ] && printf '%s\n' "$repeated" >>"$CONFIG"
  return 0
}

: >"$CONFIG"
write_config
check_eq "the file names 40 bind lines" "40" "$(grep -c '^bind ' "$CONFIG")"

WM_CONFIG="$CONFIG" "$WM_BIN" >"$WM_LOG_FILE" 2>&1 &
AWM_PID=$!
sleep 0.5
if ! kill -0 "$AWM_PID" 2>/dev/null; then
  fail "the WM is running after startup"
  tail -n 15 "$WM_LOG_FILE" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "the WM is running with the config file"

# press_until <key> <viewable-count> <what> - a synthetic press can be lost, and
# a repeat of a key that already did its work changes no state.
press_until() {
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

spawn_client 1 400 300 tagged >/dev/null
require_visible 1 "the window is viewable before any key"

press_until super+alt+F4 0 "the fortieth bind line is bound: Mod+Alt+F4 switches to tag 2"
press_until super+F1 1 "the first bind line is bound: Mod+F1 switches back to tag 1"

check_eq "no bind line was refused" "0" \
  "$(grep -cF "ERROR: config $CONFIG:" "$WM_LOG_FILE" 2>/dev/null || true)"

# An edit that repeats line 1's chord: the reload has to say which line it
# dropped and why, and that it is a chord already in the table rather than a
# table with no room.
: >"$CONFIG"
write_config "bind Mod+F1 tag-manager.view 2"
check_eq "the edit names 41 bind lines" "41" "$(grep -c '^bind ' "$CONFIG")"

kill -HUP "$AWM_PID" 2>/dev/null
sleep 0.5

check_eq "the repeated chord is reported against the line that repeated it" "1" \
  "$(grep -cF "ERROR: config $CONFIG:41: cannot bind 'F1' to 'tag-manager.view'" \
    "$WM_LOG_FILE" 2>/dev/null || true)"
check_eq "the message names the chord it is already bound to" "1" \
  "$(grep -cF "the chord is already bound to 'tag-manager.view'" "$WM_LOG_FILE" 2>/dev/null || true)"

if kill -0 "$AWM_PID" 2>/dev/null; then
  pass "the WM still runs after the reload"
else
  fail "the WM died on the reload"
fi

finish
