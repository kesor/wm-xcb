#!/usr/bin/env bash
# A client process that dies while it holds the focus leaves the focus on a
# surviving client, and the WM stays up.
#
# The client is killed with SIGKILL, so its connection closes instead of
# sending a destroy request: the WM learns about the windows from the server's
# teardown. Focus left on a window that no longer exists swallows every key and
# nothing in the session can take it back.
source "$(dirname "$0")/lib.sh"

press super+1
sleep 0.3

# One window per process, so the pid recorded for a spawn is the client behind
# exactly one of the window ids.
mapfile -t first < <(spawn_client 1 400 300 killedfirst)
first_pid=$(last_client_pid)
mapfile -t second < <(spawn_client 1 400 300 killedsecond)
second_pid=$(last_client_pid)
require_visible 2 "both windows are viewable"

focused=$(focused_window)
victim=""
survivor=""
if [ "$((focused))" = "$((second[0]))" ]; then
  victim=$second_pid
  survivor=${first[0]}
elif [ "$((focused))" = "$((first[0]))" ]; then
  victim=$first_pid
  survivor=${second[0]}
fi
if [ -z "$victim" ]; then
  fail "focus is not on either managed client ('$focused'), so there is nothing to kill"
  finish
  exit 1
fi
pass "focus is on a managed client ($focused)"

kill -KILL "$victim" 2>/dev/null

check_eq "the killed client's window is gone" "1" "$(count_viewable)"
if wait_focus "$survivor" 2000; then
  pass "focus moved to the surviving client ($survivor)"
else
  fail "focus did not move to the surviving client: it is on '$(focused_window)'"
fi
check_wm_alive

finish
