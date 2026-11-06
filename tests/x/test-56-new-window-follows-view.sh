#!/usr/bin/env bash
# A window mapped while tag 2 is viewed belongs to tag 2, not to the tag that
# was visible when the monitor was created. Before the tag view was the only
# store, the new client took tag 1 from Monitor.tagset and stayed on screen on
# tag 2 until a view change happened to unmatch it.
source "$(dirname "$0")/lib.sh"

# property_value - the value half of the line xprop prints.
property_value() {
  sed -e 's/^[^=:]*[=:][[:space:]]*//' -e 's/^window id # //'
}

root_value() {
  xprop -root -notype "$1" 2>/dev/null | property_value
}

window_value() {
  xprop -id "$1" -notype "$2" 2>/dev/null | property_value
}

press super+2
sleep 0.4
check_eq "the view moved to desktop 2" "1" "$(root_value _NET_CURRENT_DESKTOP)"

mapfile -t ids < <(spawn_client 1 400 300 follows)
require_visible 1 "the new window is visible on the viewed tag"
check_eq "the new window carries the viewed tag" "1" \
  "$(window_value "${ids[0]}" _NET_WM_DESKTOP)"

press super+1
sleep 0.4
check_eq "the window hides when the view leaves its tag" "0" "$(count_viewable)"

check_wm_alive
finish
