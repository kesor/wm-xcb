#!/usr/bin/env bash
# MOD+f must make the focused window fullscreen in the way other clients can
# see: the WM sets _NET_WM_STATE on the window to the fullscreen atom, and
# removes it again when the key is pressed a second time. A state machine that
# moved while the window's property stayed empty left the client, and every tool
# that reads the property, with no idea it was fullscreen.
source "$(dirname "$0")/lib.sh"

# wm_state_has_fullscreen <window-id> - 0 when the property the WM set lists
# _NET_WM_STATE_FULLSCREEN. xprop prints "not found" on a window that has no
# property, so the whole line is read rather than a single word.
wm_state_has_fullscreen() {
  xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q '_NET_WM_STATE_FULLSCREEN'
}

# wait_wm_state <window-id> <yes|no> [timeout_ms] - the property is written by
# the WM while the key event is handled, so a read straight after the key races
# it. <yes> waits for the atom to appear, <no> for it to go.
wait_wm_state() {
  local want=$1 id=$2 timeout=${3:-2000} waited=0
  while [ "$waited" -lt "$timeout" ]; do
    if wm_state_has_fullscreen "$id"; then
      [ "$want" = yes ] && return 0
    else
      [ "$want" = no ] && return 0
    fi
    sleep 0.02
    waited=$((waited + 20))
  done
  [ "$want" = no ] && ! wm_state_has_fullscreen "$id"
}

spawn_client 1 400 300 fullscreen >/dev/null
require_visible 1 "window visible before the key"

id=$(xdotool search --onlyvisible --class "$CLASS" | head -n 1)
if [ -z "$id" ]; then
  fail "no viewable window to make fullscreen"
  finish
  exit $?
fi

check_eq "_NET_WM_STATE_FULLSCREEN is absent on a windowed client" "" \
  "$(xprop -id "$id" _NET_WM_STATE 2>/dev/null | grep '_NET_WM_STATE_FULLSCREEN')"

press super+f
if wait_wm_state yes "$id" 2000; then
  pass "MOD+f sets _NET_WM_STATE_FULLSCREEN on the window"
else
  fail "MOD+f left _NET_WM_STATE without the fullscreen atom: $(xprop -id "$id" _NET_WM_STATE 2>/dev/null)"
fi

press super+f
if wait_wm_state no "$id" 2000; then
  pass "MOD+f again removes _NET_WM_STATE_FULLSCREEN"
else
  fail "a second MOD+f left the fullscreen atom set: $(xprop -id "$id" _NET_WM_STATE 2>/dev/null)"
fi

finish
