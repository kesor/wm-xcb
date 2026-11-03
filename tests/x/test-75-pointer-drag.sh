#!/usr/bin/env bash
# Dragging with a modifier held moves a client, and dragging its corner resizes
# it. Both are BUTTON_PRESS, a stream of MOTION_NOTIFY and BUTTON_RELEASE on the
# client window, so a WM that only handles keyboard and map events leaves the
# pointer to the client and nothing moves.
#
# The press and the motion are sent as separate xdotool commands with a pause
# between them, so the server delivers a press and then several motion events
# rather than one jump.
#
# The client it drags is tiled, because nothing floats yet and this is the whole
# of the pointer promise today. The assertion reads the geometry after the
# button is released and requires it to have changed. A layout that puts the
# tiled client back, which is what dwm's movemouse does on the next arrange,
# fails here. A pointer component that moves only floating clients has to
# change this test to float the client first, not to weaken the assertion.
source "$(dirname "$0")/lib.sh"

DRAG_X=120
DRAG_Y=80
MIN_MOVE=50
MIN_GROW=50

press super+1
sleep 0.3
mapfile -t ids < <(spawn_client 2 300 200 dragging)
require_visible 2 "both windows are viewable"

win=$(focused_window)
matched=0
for id in "${ids[@]}"; do
  [ "$((id))" = "$((win))" ] && matched=1
done
if [ "$matched" -ne 1 ]; then
  fail "focus is not on either managed client ('$win'), so there is nothing to drag"
  finish
  exit 1
fi
pass "the client to drag is focused ($win)"

read -r won_x won_y won_w won_h <<<"$(rect_of "$win")"

# Drag from the middle of the client, away from any corner.
xdotool keydown super
xdotool mousemove $((won_x + won_w / 2)) $((won_y + won_h / 2))
sleep 0.1
xdotool mousedown 1
sleep 0.1
for step in 1 2 3 4; do
  xdotool mousemove $((won_x + won_w / 2 + DRAG_X * step / 4)) \
    $((won_y + won_h / 2 + DRAG_Y * step / 4))
  sleep 0.05
done
xdotool mouseup 1
xdotool keyup super
sleep 0.3

read -r moved_x moved_y _ _ <<<"$(rect_of "$win")"
if [ "$((moved_x - won_x))" -ge "$MIN_MOVE" ] || [ "$((won_x - moved_x))" -ge "$MIN_MOVE" ]; then
  pass "MOD+button1 drag moves the client (to ${moved_x},${moved_y})"
else
  fail "MOD+button1 drag moves the client: it is still at ${moved_x},${moved_y}"
fi

# Now the opposite corner: drag it out and the client has to grow.
read -r grown_x grown_y grown_w grown_h <<<"$(rect_of "$win")"
xdotool keydown super
xdotool mousemove $((grown_x + grown_w + 1)) $((grown_y + grown_h + 1))
sleep 0.1
xdotool mousedown 3
sleep 0.1
for step in 1 2 3 4; do
  xdotool mousemove $((grown_x + grown_w + 1 + DRAG_X * step / 4)) \
    $((grown_y + grown_h + 1 + DRAG_Y * step / 4))
  sleep 0.05
done
xdotool mouseup 3
xdotool keyup super
sleep 0.3

read -r _ _ resized_w resized_h <<<"$(rect_of "$win")"
if [ "$((resized_w - grown_w))" -ge "$MIN_GROW" ] && [ "$((resized_h - grown_h))" -ge "$MIN_GROW" ]; then
  pass "MOD+button3 drag on the corner resizes the client (to ${resized_w}x${resized_h})"
else
  fail "MOD+button3 drag on the corner resizes the client: it is ${resized_w}x${resized_h}"
fi

check_wm_alive
finish
