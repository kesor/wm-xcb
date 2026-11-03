#!/usr/bin/env bash
# Moving the pointer onto a managed window moves the input focus to it.
#
# The focus component raw-writes the client's FocusSM from the pointer enter and
# leave events its handler receives. A managed window that does not select
# EnterWindow sends those events to the root instead, where the handler finds no
# client, so the focus never follows the pointer.
#
# The server sends no enter event when the pointer is already inside the window
# it is moved to. A move onto the window that already has focus passes without
# the WM doing anything: the last managed client is focused, and the pointer at
# rest is often in the other one. The moves that are asserted both cross from
# the other window, and each one refuses to pass if focus was already on the
# target.
source "$(dirname "$0")/lib.sh"

press super+1
sleep 0.3

mapfile -t first < <(spawn_client 1 400 300 pointerfirst)
mapfile -t second < <(spawn_client 1 400 300 pointersecond)
require_visible 2 "both windows are viewable"

warp_to() { # warp_to <window-id>
  local x y w h
  read -r x y w h <<<"$(rect_of "$1")"
  xdotool mousemove $((x + w / 2)) $((y + h / 2)) 2>/dev/null
}

# Assert focus moves onto this window because the pointer entered it. A focus
# that was already there is a failure: the move did not have to change anything.
assert_followed() { # assert_followed <window-id> <name>
  local id=$1 name=$2 before
  before=$(focused_window)
  if [ -n "$before" ] && [ "$((before))" = "$((id))" ]; then
    fail "focus was already on the $name client ($id), so a move onto it proves nothing"
    return
  fi
  warp_to "$id"
  if wait_focus "$id" 1000; then
    pass "focus follows the pointer onto the $name client ($id)"
  else
    fail "focus did not follow the pointer onto the $name client ($id): it is on '$(focused_window)'"
  fi
}

# Park the pointer in the first window. This is not an assertion: it may already
# be there, so the server may send no enter, and focus may stay on the last
# managed client. Wait until the focus stops changing, so the branch below does
# not read a window the park enter has not landed on yet.
warp_to "${first[0]}"
focused=""
prev=""
for _ in 1 2 3 4 5 6; do
  sleep 0.05
  focused=$(focused_window)
  if [ -n "$prev" ] && [ -n "$focused" ] && [ "$((prev))" = "$((focused))" ]; then
    break
  fi
  prev=$focused
done

if [ -n "$focused" ] && [ "$((focused))" = "$((first[0]))" ]; then
  assert_followed "${second[0]}" second
  assert_followed "${first[0]}" first
elif [ -n "$focused" ] && [ "$((focused))" = "$((second[0]))" ]; then
  # Pointer is in the first window and focus is on the second. Entering the
  # second would not change focus. Cross there without asserting, then both
  # returns leave a window and enter the other.
  warp_to "${second[0]}"
  assert_followed "${first[0]}" first
  assert_followed "${second[0]}" second
else
  fail "focus is not on either managed client ('$focused'), so there is nothing to follow"
fi

check_wm_alive
finish
