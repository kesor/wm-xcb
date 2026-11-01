#!/usr/bin/env bash
# The WM decides which client has input focus. A focus value of PointerRoot or
# None means it never called xcb_set_input_focus.
source "$(dirname "$0")/lib.sh"

mapfile -t ids < <(spawn_client 2 400 300 focused)
wait_viewable 2 2000 >/dev/null

focus=$(focused_window)
if [ -z "$focus" ]; then
  fail "xdotool cannot read an input focus: the WM never calls xcb_set_input_focus (STATUS 4.4)"
  finish
  exit 1
fi
pass "X reports an input focus ($focus)"

if [ "$focus" = "1" ] || [ "$focus" = "0" ]; then
  fail "input focus is PointerRoot/None: the WM never calls xcb_set_input_focus (STATUS 4.4)"
else
  matched=0
  for id in "${ids[@]}"; do
    [ "$((id))" = "$((focus))" ] && matched=1
  done
  check_eq "focused window is one of the managed clients" "1" "$matched"
fi

finish
