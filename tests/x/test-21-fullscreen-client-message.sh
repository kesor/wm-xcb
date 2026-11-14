#!/usr/bin/env bash
# A client asks for fullscreen the way EWMH says to: it sends a _NET_WM_STATE
# ClientMessage to the root window, because the property belongs to the window
# manager. A video player or a browser pressing F11 does this instead of
# pressing MOD+f, and a WM that only reads the key leaves the window windowed
# while the client believes it is fullscreen.
#
# The request is a state, and the only request this WM has is a toggle, so a
# repeated ADD must not unfullscreen the window.
source "$(dirname "$0")/lib.sh"

# wm_state_has_fullscreen <window-id> - 0 when the property the WM set lists
# _NET_WM_STATE_FULLSCREEN. xprop prints "not found" on a window that has no
# property, so the whole line is read rather than a single word.
wm_state_has_fullscreen() {
  xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q '_NET_WM_STATE_FULLSCREEN'
}

# wait_wm_state <yes|no> <window-id> [timeout_ms] - the WM writes the property
# after the message is handled, so a read straight after the request races it.
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
require_visible 2 "both windows are visible before the request"

mapfile -t ids < <(xdotool search --onlyvisible --class "$CLASS" | head -n 2)
if [ "${#ids[@]}" -ne 2 ]; then
  fail "expected two viewable windows, found ${#ids[@]}"
  finish
  exit $?
fi
id=${ids[0]}
other=${ids[1]}

check_eq "_NET_WM_STATE_FULLSCREEN is absent before the request" "" \
  "$(xprop -id "$id" _NET_WM_STATE 2>/dev/null | grep '_NET_WM_STATE_FULLSCREEN')"

xdotool windowstate --add FULLSCREEN "$id" 2>/dev/null
if wait_wm_state yes "$id" 2000; then
  pass "a client's _NET_WM_STATE ADD makes the WM write the fullscreen atom"
else
  fail "the ADD was ignored: $(xprop -id "$id" _NET_WM_STATE 2>/dev/null)"
fi

sleep 0.2
check_eq "the fullscreen window covers the screen" "0 0 $(screen_size | awk '{print $1, $2}')" \
  "$(outer_rect "$id")"
check_eq "the other window is tiled alone on the monitor" "0 0 $(screen_size | awk '{print $1, $2}')" \
  "$(outer_rect "$other")"

# The same ADD again. The client is stating a state it already asked for, and
# the WM's only request is a toggle, so a handler that forwarded it blindly
# would take the window back out of fullscreen here.
xdotool windowstate --add FULLSCREEN "$id" 2>/dev/null
sleep 0.3
if wm_state_has_fullscreen "$id"; then
  pass "a repeated ADD leaves the fullscreen atom set"
else
  fail "a repeated ADD unfullscreened the window: $(xprop -id "$id" _NET_WM_STATE 2>/dev/null)"
fi

xdotool windowstate --remove FULLSCREEN "$id" 2>/dev/null
if wait_wm_state no "$id" 2000; then
  pass "a client's _NET_WM_STATE REMOVE makes the WM drop the fullscreen atom"
else
  fail "the REMOVE was ignored: $(xprop -id "$id" _NET_WM_STATE 2>/dev/null)"
fi

sleep 0.2
check_eq "the two windows tile the monitor again, without overlapping" "0" \
  "$(overlap_pairs "$id" "$other")"

# TOGGLE has to follow the machine: the window is windowed now, so it goes
# fullscreen. A handler that compared the action with a fixed state would not.
xdotool windowstate --toggle FULLSCREEN "$id" 2>/dev/null
if wait_wm_state yes "$id" 2000; then
  pass "a client's _NET_WM_STATE TOGGLE makes a windowed client fullscreen"
else
  fail "the TOGGLE was ignored: $(xprop -id "$id" _NET_WM_STATE 2>/dev/null)"
fi

check_wm_alive
finish
