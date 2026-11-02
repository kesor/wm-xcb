#!/usr/bin/env bash
# A grabbed key still fires while Num_Lock is on. The server adds the NumLock
# modifier bit to the key press, so a grab and a lookup that only know the bare
# modifiers leave every binding dead for as long as NumLock is on.
source "$(dirname "$0")/lib.sh"

num_lock_state() {
  xset q 2>/dev/null | awk '/Num Lock:/{for (i = 1; i <= NF; i++) if ($i == "Num" && $(i+1) == "Lock:") { print $(i+2); exit }}'
}

spawn_client 1 400 300 numlock >/dev/null
require_visible 1 "window visible before the key"

xdotool key Num_Lock 2>/dev/null
sleep 0.2

# A precondition, not a result: with Num_Lock off the press below passes even
# if the grabs ignored the NumLock mask entirely.
if [ "$(num_lock_state)" != "on" ]; then
  fail "Num_Lock did not turn on ('$(num_lock_state)'), so the press would not test the numlock grab"
  finish
  exit 1
fi
pass "Num_Lock is on"

# No --clearmodifiers here: it drops the modifiers xdotool can see, and the
# point of this press is that it arrives with Num_Lock on.
xdotool key super+2 2>/dev/null
sleep 0.4

hidden=$(count_viewable)
xdotool key Num_Lock 2>/dev/null

check_eq "MOD+2 hides its window while Num_Lock is on" "0" "$hidden"

# Leave the monitor on tag 1: the tests that follow put a new client on tag 1
# and press MOD+2 themselves, which changes nothing if this test left tag 2
# showing. The window does not come back here because the unmap this tag change
# caused also unmanaged the client, which is a separate defect.
xdotool key super+1 2>/dev/null
sleep 0.2

finish
