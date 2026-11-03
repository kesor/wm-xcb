#!/usr/bin/env bash
# A tag switch away and back in one burst must leave the client managed.
#
# The loop drains every queued event before it flushes, so two tag actions that
# arrive together run a hide for the old tag and a show for the new one before
# either UnmapNotify is read. The unmap still belongs to the hide and must not
# be taken for the client withdrawing: the window is on screen again by then.
#
# A slow switch does not reach this ordering, so the two keys are sent while the
# WM is stopped and the server has both of them queued.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

# Every exit path resumes a stopped WM, so one failure cannot leave the rest of
# the suite talking to a frozen process.
resume_wm() {
  kill -CONT "$WM_PID" 2>/dev/null
}
trap resume_wm EXIT

wm_state() { # wm_state <window-id>
  xprop -id "$1" WM_STATE 2>/dev/null |
    awk -F': ' '/window state/{print $2; exit}'
}

spawn_client 1 400 300 tagged >/dev/null
wait_viewable 1 2000 >/dev/null

win=$(viewable_ids | head -n 1)
check_nonempty "the client window id" "$win"
check_eq "WM_STATE is Normal before the keys" "Normal" "$(wm_state "$win")"

kill -STOP "$WM_PID"
xdotool key --delay 0 --clearmodifiers super+2 super+1 2>/dev/null
kill -CONT "$WM_PID"

# The drain that reads both keys also sends the unmap and the map; the
# UnmapNotify comes back one iteration later.
sleep 0.6

check_eq "the window is viewable after the burst" "1" "$(count_viewable)"
check_eq "WM_STATE is still Normal after the burst" "Normal" "$(wm_state "$win")"

# Still managed: a tag switch away hides the window. A client the WM unmanaged
# on the false withdrawal would be left alone by the same key.
press super+2
sleep 0.4
check_eq "MOD+2 still hides the window" "0" "$(count_viewable)"

check_wm_alive
finish
