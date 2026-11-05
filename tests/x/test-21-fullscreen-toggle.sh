#!/usr/bin/env bash
# MOD+f must make the focused window fullscreen in the way other clients and
# the user can see: the WM sets _NET_WM_STATE on the window to the fullscreen
# atom, and the window covers its monitor instead of sharing it. A state
# machine that moved while the property stayed empty, and while the window kept
# its tiled rectangle, left the client with no idea it was fullscreen.
source "$(dirname "$0")/lib.sh"

# wm_state_has_fullscreen <window-id> - 0 when the property the WM set lists
# _NET_WM_STATE_FULLSCREEN. xprop prints "not found" on a window that has no
# property, so the whole line is read rather than a single word.
wm_state_has_fullscreen() {
  xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q '_NET_WM_STATE_FULLSCREEN'
}

# wait_wm_state <yes|no> <window-id> [timeout_ms] - the property is written by
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

# outer_rect <window-id> - prints "x y w h" with the border added, the
# footprint the user sees.
outer_rect() {
  local id=$1 x y w h border
  read -r x y w h <<<"$(rect_of "$id")"
  border=$(border_of "$id")
  printf '%s %s %s %s\n' "$x" "$y" "$((w + 2 * border))" "$((h + 2 * border))"
}

spawn_client 2 400 300 fullscreen >/dev/null
require_visible 2 "both windows visible before the key"

focused=$(focused_window)
if ! all_ids | grep -qx "$focused"; then
  fail "the X input focus is not on either of this test's windows (got '$focused'); the key would reach another client"
  finish
  exit $?
fi
pass "the focused window is one of this test's clients"

check_eq "_NET_WM_STATE_FULLSCREEN is absent on a windowed client" "" \
  "$(xprop -id "$focused" _NET_WM_STATE 2>/dev/null | grep '_NET_WM_STATE_FULLSCREEN')"

press super+f
if wait_wm_state yes "$focused" 2000; then
  pass "MOD+f sets _NET_WM_STATE_FULLSCREEN on the focused window"
else
  fail "MOD+f left _NET_WM_STATE without the fullscreen atom: $(xprop -id "$focused" _NET_WM_STATE 2>/dev/null)"
fi

sleep 0.2
check_eq "the fullscreen window covers the screen" "0 0 $(screen_size | awk '{print $1, $2}')" \
  "$(outer_rect "$focused")"

other=$(all_ids | grep -vx "$focused" | head -n 1)
check_eq "the other window is tiled alone on the monitor" "0 0 $(screen_size | awk '{print $1, $2}')" \
  "$(outer_rect "$other")"

press super+f
if wait_wm_state no "$focused" 2000; then
  pass "MOD+f again removes _NET_WM_STATE_FULLSCREEN"
else
  fail "a second MOD+f left the fullscreen atom set: $(xprop -id "$focused" _NET_WM_STATE 2>/dev/null)"
fi

sleep 0.2
check_eq "the two windows tile the monitor again, without overlapping" "0" \
  "$(overlap_pairs "$focused" "$other")"
check_le "the window that left fullscreen is no longer the whole screen" \
  "$(( $(screen_size | awk '{print $1}') - 1 ))" \
  "$(outer_rect "$focused" | awk '{print $3}')"

finish
