#!/usr/bin/env bash
# A window mapped while the view is on another tag is not put on screen.
#
# A client keeps the tags it was given when it first appeared. Its client can
# withdraw the window and map it again, and the view can have moved off that tag
# in between; mapping the window on that second map request puts it on screen on
# a tag it is not on, where nothing takes it off screen until some later view
# change happens to unmatch it.
source "$(dirname "$0")/lib.sh"

# property_value - the value half of the line xprop prints.
property_value() {
  sed -e 's/^[^=:]*[=:][[:space:]]*//' -e 's/^window id # //'
}

root_value() {
  xprop -root -notype "$1" 2>/dev/null | property_value
}

mapfile -t ids < <(spawn_client 1 400 300 respecttags)
wid=${ids[0]}
require_visible 1 "the window is on screen while the view is on its tag"

# windowunmap sends the unmap request the client would send itself: the WM stops
# managing the client and keeps it, which is what a client that hides its window
# and shows it again does.
xdotool windowunmap "$wid"
sleep 0.3
check_eq "the window is off screen after the client withdrew it" "0" "$(count_viewable)"

# The view moves off the tag the client is on.
press super+3
sleep 0.4
check_eq "the view moved to desktop 3" "2" "$(root_value _NET_CURRENT_DESKTOP)"

# The map lines are DEBUG output, and the window was mapped once already: what
# this looks for is that the second map request adds none. A WM built without
# DEBUG has no map lines to read.
maps_before=$(grep -cF "Mapped client $((wid))" "$WM_LOG" 2>/dev/null || true)

# The client maps its window again, with the view on another tag.
xdotool windowmap "$wid"
sleep 0.4
check_eq "the window is not on screen on a tag the view is not on" "0" "$(count_viewable)"
check_eq "the WM says why the client stays off screen" "1" \
  "$(grep -cF "client $((wid)) stays off screen" "$WM_LOG" 2>/dev/null || true)"
if grep -q '^DEBUG:' "$WM_LOG" 2>/dev/null; then
  check_eq "the WM log has no map for the second map request" "$maps_before" \
    "$(grep -cF "Mapped client $((wid))" "$WM_LOG" 2>/dev/null || true)"
fi

# Still managed, and still on its own tag: showing that tag puts it on screen.
press super+1
sleep 0.4
check_eq "the window comes back on the tag it is on" "1" "$(count_viewable)"

check_wm_alive
finish
