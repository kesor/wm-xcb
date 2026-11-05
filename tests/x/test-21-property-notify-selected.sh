#!/usr/bin/env bash
#
# test-21-property-notify-selected.sh - a managed window reports its own
# property changes to the WM.
#
# _NET_WM_STATE is where a fullscreen client, or another program acting for it,
# records the state it wants. A window sends no PropertyNotify unless someone
# selected PropertyChange on it, and an event mask belongs to one window, so the
# WM has to select it on every client it manages - a client adopted at startup
# included, since that path goes through the same manage code. Without the
# selection the fullscreen component's handler never runs and its state machine
# can never follow a change it was not told about.
#
# The window here asks for no events of its own. `xwininfo -events` reports the
# union of every client's selection, so a window that selects PropertyChange
# itself - which is what tools/xclient does unless -e is given - makes the check
# pass whether or not the WM selected anything.
#
# The fullscreen state the WM derives from a change has no X-visible effect yet:
# no component draws from it, and the _NET_WM_STATE write the WM sends is a
# client message nothing handles. So this file asserts the selection, and that
# the notifications which now arrive are read without stopping the loop - not
# the state itself.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

id=$(spawn_client 1 400 300 propnotify -e | head -n 1)
require_visible 1 "the window with no event mask of its own is viewable"

events=$(xwininfo -id "$id" -events 2>/dev/null)
for want in EnterWindow LeaveWindow PropertyChange; do
  case "$events" in
    *"$want"*) pass "the managed window selects $want" ;;
    *)
      fail "the managed window selects $want: xwininfo -events says '$(printf '%s' "$events" | tr '\n' ' ')'"
      ;;
  esac
done

atom_fullscreen="_NET_WM_STATE_FULLSCREEN"
xprop -id "$id" -f _NET_WM_STATE 32a -set _NET_WM_STATE "$atom_fullscreen" 2>/dev/null
check_eq "the client's _NET_WM_STATE holds the fullscreen atom" \
  "1" "$(xprop -id "$id" _NET_WM_STATE 2>/dev/null | grep -c "$atom_fullscreen")"

# The WM answers a PropertyNotify with a GetProperty of its own, so the change is
# only read one round trip later, and nothing it does with the answer is visible
# in X. A fixed settle is the only wait available here.
sleep 0.3

xprop -id "$id" -remove _NET_WM_STATE 2>/dev/null
check_eq "the client's _NET_WM_STATE is gone" \
  "0" "$(xprop -id "$id" _NET_WM_STATE 2>/dev/null | grep -c "$atom_fullscreen")"

sleep 0.3

check_wm_alive
require_visible 1 "the window is still viewable after two _NET_WM_STATE changes"

case "$(xprop -id "$id" WM_STATE 2>/dev/null)" in
  *Normal*) pass "the window is still managed" ;;
  *) fail "the window is still managed: '$(xprop -id "$id" WM_STATE 2>/dev/null)'" ;;
esac

finish
