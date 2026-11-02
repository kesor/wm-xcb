#!/usr/bin/env bash
# Focus moves to the client that is left when the focused one closes. A closed
# window that X still points at swallows every key, and nothing in the session
# can give the focus back.
source "$(dirname "$0")/lib.sh"

mapfile -t first < <(spawn_client 1 400 300 focusfirst)
mapfile -t second < <(spawn_client 1 400 300 focussecond)
require_visible 2 "both windows visible"

focused=$(focused_window)
if [ -z "$focused" ]; then
  fail "xdotool cannot read an input focus, so there is nothing to close"
  finish
  exit 1
fi

remaining=""
matched=0
for id in "${first[0]}" "${second[0]}"; do
  if [ "$((id))" = "$((focused))" ]; then
    matched=1
  else
    remaining="$id"
  fi
done
if [ "$matched" -ne 1 ]; then
  fail "focus is not on either managed client ('$focused')"
  finish
  exit 1
fi
pass "focus is on a managed client ($focused)"

# XKillClient takes the whole connection of the client that owns the window, so
# each window comes from its own process: killing one must not kill the other.
xdotool windowkill "$focused" 2>/dev/null
sleep 0.5

check_eq "focus moves to the client that is left" "$((remaining))" "$(( $(focused_window) ))"
check_wm_alive

finish
