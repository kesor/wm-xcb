#!/usr/bin/env bash
# Moving the pointer onto a managed window moves the input focus to it.
#
# The focus component raw-writes the client's FocusSM from the pointer enter and
# leave events its handler receives. A managed window that does not select
# EnterWindow sends those events to the root instead, where the handler finds no
# client, so the focus never follows the pointer.
#
# The server sends no enter event when the pointer is already inside the window
# it is moved to, so the first move is always onto the client the pointer is not
# in right now. Without that order a test passes on the focus the WM left on the
# last managed client.
source "$(dirname "$0")/lib.sh"

press super+1
sleep 0.3

mapfile -t first < <(spawn_client 1 400 300 pointerfirst)
mapfile -t second < <(spawn_client 1 400 300 pointersecond)
require_visible 2 "both windows are viewable"

pointer_inside() { # pointer_inside <window-id> <pointer-x> <pointer-y>
  local x y w h
  read -r x y w h <<<"$(rect_of "$1")"
  [ "$2" -ge "$x" ] && [ "$2" -lt $((x + w)) ] && [ "$3" -ge "$y" ] && [ "$3" -lt $((y + h)) ]
}

move_onto() { # move_onto <window-id> <name>
  local x y w h
  read -r x y w h <<<"$(rect_of "$1")"
  xdotool mousemove $((x + w / 2)) $((y + h / 2)) 2>/dev/null
  if wait_focus "$1" 1000; then
    pass "focus follows the pointer onto the $2 client ($1)"
  else
    fail "focus did not follow the pointer onto the $2 client ($1): it is on '$(focused_window)'"
  fi
}

read -r pointer_x pointer_y <<<"$(xdotool getmouselocation --shell 2>/dev/null |
  awk -F= '/^X=/{x=$2}/^Y=/{y=$2}END{print x+0, y+0}')"

if pointer_inside "${first[0]}" "$pointer_x" "$pointer_y"; then
  move_onto "${second[0]}" second
  move_onto "${first[0]}" first
else
  move_onto "${first[0]}" first
  move_onto "${second[0]}" second
fi

check_wm_alive
finish
