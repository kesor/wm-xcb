#!/usr/bin/env bash
#
# test-56-config-reload.sh - SIGHUP rereads the config file without a restart.
#
# The file is read at startup, so the only way to know that a later edit reaches
# the running WM is to edit the file under a live WM and send it SIGHUP. This
# test starts its own display and its own WM for that reason.
#
# Assertions:
#   - a binding the edit adds works after SIGHUP, and one it removes stops
#     working, with no restart
#   - the managed client keeps its geometry across the reload
#   - a file with a line the parser rejects is refused with one error line that
#     names the line, and the bindings already running still work
#   - removing the file and asking again brings the compiled bindings back

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

DIR="$TMPDIR_SEAM/config-reload"
rm -rf "$DIR"
mkdir -p "$DIR"
XVFB_LOG="$DIR/xvfb.log"
CONFIG="$DIR/config"
WM_LOG_FILE="$DIR/wm.log"

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

LOAD_DISPLAY=":$DISPLAY_NUM"
XVFB_PID=""
AWM_PID=""

cleanup() {
  [ -n "$AWM_PID" ] && kill "$AWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$LOAD_DISPLAY" -screen 0 1280x800x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

up=0
for _ in $(seq 1 100); do
  if DISPLAY="$LOAD_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    up=1
    break
  fi
  sleep 0.05
done
if [ "$up" -ne 1 ]; then
  fail "a private display is up ($LOAD_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($LOAD_DISPLAY)"

export DISPLAY="$LOAD_DISPLAY"

# MOD+F6 comes back to tag 1 in every file this test writes, so the assertion
# that a binding survives an edit has something to check against.
write_config() {
  cat >"$CONFIG" <<'CONFIG_FILE'
bind Mod+F5 tag-manager.view 2
bind Mod+F6 tag-manager.view 1
CONFIG_FILE
}

write_config

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

# wait_for_log <fixed-text> <minimum-count> <what>
wait_for_log() {
  local text=$1 want=$2 what=$3 waited=0 got=0

  while [ "$waited" -lt 3000 ]; do
    got=$(grep -cF "$text" "$WM_LOG_FILE" 2>/dev/null || true)
    if [ "${got:-0}" -ge "$want" ]; then
      pass "$what"
      return 0
    fi
    sleep 0.05
    waited=$((waited + 50))
  done

  fail "$what: '$text' is not in the log $want time(s)"
  tail -n 15 "$WM_LOG_FILE" 2>/dev/null | sed 's/^/        /'
  return 1
}

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

# press_without_effect <key> <count> <what> - press a key that must do nothing.
# A lost press would pass this for the wrong reason, so press it several times.
press_without_effect() {
  local key=$1 want=$2 what=$3

  for _ in 1 2 3 4 5; do
    press "$key"
    sleep 0.05
  done
  sleep 0.2
  check_eq "$what" "$want" "$(count_viewable)"
}

spawn_client 1 400 300 tagged >/dev/null
require_visible 1 "the window is viewable before any key"

press_until super+F5 0 "MOD+F5 from the first file switched to tag 2"
press_until super+F6 1 "MOD+F6 from the first file switched back to tag 1"
BEFORE=$(rect_of "$(viewable_ids | head -n 1)")

# The edit an ordinary user makes: one key added, one removed, the rest left.
cat >"$CONFIG" <<'CONFIG_FILE'
bind Mod+F7 tag-manager.view 2
bind Mod+F6 tag-manager.view 1
CONFIG_FILE
kill -HUP "$AWM_PID" 2>/dev/null
wait_for_log "INFO:  config $CONFIG" 1 "SIGHUP applied the edited file"

check_eq "the reloaded bindings did not move the client" "$BEFORE" \
  "$(rect_of "$(viewable_ids | head -n 1)")"

press_until super+F7 0 "MOD+F7 from the edited file switched to tag 2"
press_until super+F6 1 "MOD+F6 still switches back to tag 1"
press_without_effect super+F5 1 "MOD+F5 no longer has a binding after the edit"

# A bad line in the same edit: the parser refuses the whole file, so the new
# binding on line 1 must not be taken and the running one must still work.
BEFORE_ERRORS=$(grep -cF "ERROR: config $CONFIG:" "$WM_LOG_FILE" 2>/dev/null || true)
cat >"$CONFIG" <<'CONFIG_FILE'
bind Mod+F8 tag-manager.view 2
bogus keybinding line
CONFIG_FILE
kill -HUP "$AWM_PID" 2>/dev/null
wait_for_log "ERROR: config $CONFIG:2:" 1 "the refused file is reported at its line"

check_eq "the refused file costs one error line, not a page" \
  "$((BEFORE_ERRORS + 1))" "$(grep -cF "ERROR: config $CONFIG:" "$WM_LOG_FILE" 2>/dev/null || true)"
press_without_effect super+F8 1 "the refused file bound no key"

press_until super+F7 0 "MOD+F7 still works after the refused reload"
press_until super+F6 1 "MOD+F6 still works after the refused reload"

# Removing the file brings the compiled bindings back, the same restart path
# test-54 covers: MOD+2 is in the compiled table and in none of these files.
rm -f "$CONFIG"
kill -HUP "$AWM_PID" 2>/dev/null
wait_for_log "INFO:  config $CONFIG: no readable file" 1 "SIGHUP with the file gone applied the compiled configuration"
press_until super+2 0 "MOD+2 from the compiled bindings works again"

if kill -0 "$AWM_PID" 2>/dev/null; then
  pass "the WM still runs after the reloads"
else
  fail "the WM died during a reload"
  tail -n 15 "$WM_LOG_FILE" 2>/dev/null | sed 's/^/        /'
fi

finish
