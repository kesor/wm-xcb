#!/usr/bin/env bash
# A client that changes its size constraints while it is running is laid out
# again with the new ones, in both directions: a minimum it adds makes the
# window grow, and removing the property makes it shrink back to the area the
# layout gives it.
#
# The hints are read once when a client is managed, so a WM that only reads them
# there keeps a running client at the size its first hints allowed. The change
# reaches the WM as a PropertyNotify on WM_NORMAL_HINTS, which is why the window
# has to have PropertyChange selected by the WM and not just by the client.
#
# xprop writes the property while the client that owns the window keeps running,
# the same way test-21 changes _NET_WM_STATE. The WM reads WM_NORMAL_HINTS with
# XCB_GET_PROPERTY_TYPE_ANY, so the type xprop chooses does not decide what the
# WM sees; the name, the format and the 18 slots do.
source "$(dirname "$0")/lib.sh"

MIN_W=1400
MIN_H=900
# The second window's minimum, larger than the first's so the assertion that it
# came down cannot be satisfied by the first window's constraints.
BIG_W=1500
BIG_H=1000

# write_minimum <window-id> <width> <height> - publish WM_NORMAL_HINTS the way
# Xlib's XSetWMNormalHints() lays it out: 18 slots, the flag word first, and the
# minimum at slots 5 and 6. Slot 0 is PMinSize.
write_minimum() {
  xprop -id "$1" -f WM_NORMAL_HINTS 32i -set WM_NORMAL_HINTS \
    "16, 0, 0, 0, 0, $2, $3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0" 2>/dev/null
}

# wait_size <window-id> <comparison> <width> <height> [timeout_ms] - returns 0
# once both dimensions satisfy <comparison> (ge or lt). The WM answers the
# notification with a GetProperty of its own and lays the window out one round
# trip later, so the geometry has to be waited for rather than read once.
wait_size() {
  local id=$1 op=$2 want_w=$3 want_h=$4 timeout=${5:-2000} waited=0 w h
  while [ "$waited" -lt "$timeout" ]; do
    read -r _ _ w h <<<"$(rect_of "$id")"
    if [ "$op" = ge ] && [ "$w" -ge "$want_w" ] && [ "$h" -ge "$want_h" ]; then
      return 0
    fi
    if [ "$op" = lt ] && [ "$w" -lt "$want_w" ] && [ "$h" -lt "$want_h" ]; then
      return 0
    fi
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

id=$(spawn_client 1 400 300 rehinted | head -n 1)
require_visible 1 "the window is viewable before its hints change"

# The precondition, so a passing assertion below cannot be satisfied by the
# first layout: a window with no minimum is tiled into the screen, which is
# smaller than the minimum it is about to ask for.
read -r _ _ start_w start_h <<<"$(rect_of "$id")"
if [ "$start_w" -lt "$MIN_W" ] && [ "$start_h" -lt "$MIN_H" ]; then
  pass "the window starts smaller than the minimum it asks for later (${start_w}x${start_h})"
else
  fail "the window starts smaller than the minimum it asks for later: ${start_w}x${start_h} is already at or above ${MIN_W}x${MIN_H}, so this file would prove nothing"
fi

write_minimum "$id" "$MIN_W" "$MIN_H"

if wait_size "$id" ge "$MIN_W" "$MIN_H"; then
  pass "the window grows to the minimum it asked for after being managed"
else
  read -r _ _ w h <<<"$(rect_of "$id")"
  fail "the window grows to the minimum it asked for after being managed: ${w}x${h} is below ${MIN_W}x${MIN_H}"
fi

# The other direction, on a window of its own: this one is created with a
# minimum, which the layout honours, and the removal of the property has to let
# it back down to the area the layout gives it. A separate window keeps the two
# assertions independent - a WM that never grew the first window cannot make
# this one pass by leaving it where it always was.
second=$(spawn_client 1 400 300 rehinted-removed -M "${BIG_W}x${BIG_H}" | head -n 1)
if wait_size "$second" ge "$BIG_W" "$BIG_H"; then
  pass "the window created with a minimum is laid out at that minimum"
else
  read -r _ _ w h <<<"$(rect_of "$second")"
  fail "the window created with a minimum is laid out at that minimum: ${w}x${h} is below ${BIG_W}x${BIG_H}, so the removal below would prove nothing"
fi

xprop -id "$second" -remove WM_NORMAL_HINTS 2>/dev/null

if wait_size "$second" lt "$BIG_W" "$BIG_H"; then
  pass "the window shrinks back once it removes its minimum"
else
  read -r _ _ w h <<<"$(rect_of "$second")"
  fail "the window shrinks back once it removes its minimum: still ${w}x${h}"
fi

check_wm_alive
require_visible 2 "the windows are still viewable after the hint changes"

finish
