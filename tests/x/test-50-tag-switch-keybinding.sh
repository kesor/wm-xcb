#!/usr/bin/env bash
# MOD+2 must switch to tag 2. With one window on tag 1, switching away makes
# the window unmapped and switching back makes it viewable again.
source "$(dirname "$0")/lib.sh"

spawn_client 1 400 300 tagged >/dev/null
wait_viewable 1 2000 >/dev/null

require_visible 1 "window visible before the key"

press super+2
sleep 0.4
after=$(count_viewable)
if [ "$after" = "0" ]; then
  pass "MOD+2 hid the tag-1 window"
else
  fail "MOD+2 changed nothing: no key grab exists, so KEY_PRESS never reaches the WM (STATUS 4.5)"
fi

press super+1
sleep 0.4
check_eq "MOD+1 shows the window again" "1" "$(count_viewable)"

finish
