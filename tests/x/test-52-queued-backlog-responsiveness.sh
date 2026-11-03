#!/usr/bin/env bash
# A key pressed while the WM has a backlog of queued events is still answered.
#
# The loop drains every queued event before it flushes, so a burst can put the
# answer to a later key behind hundreds of earlier ones. The WM is stopped with
# SIGSTOP while 200 tag toggles and one MOD+1 are sent, so the server queues all
# 402 key events and the WM reads them in one drain. What is measured is the
# wall clock from resuming the WM until the MOD+1 that was queued with the burst
# has shown the tag-1 window: a loop that handles events one at a time without
# returning to its event sources, or one that takes a lock per event, shows up
# here as a window that does not come back inside the budget.
source "$(dirname "$0")/lib.sh"

BURST=200
BUDGET_MS=1000

now_ms() {
  echo $(($(date +%s%N) / 1000000))
}

resume_wm() {
  kill -CONT "$WM_PID" 2>/dev/null
}
trap resume_wm EXIT

spawn_client 1 400 300 backlog >/dev/null
require_visible 1 "the window is visible on tag 1"

press super+2
sleep 0.4
if [ "$(count_viewable)" -ne 0 ]; then
  fail "MOD+2 did not hide the tag-1 window, so the burst below tests nothing"
  press super+1
  finish
  exit 1
fi
pass "the window is hidden on tag 2 before the burst"

keys=()
for _ in $(seq 1 "$BURST"); do
  keys+=(super+shift+2)
done
# The toggles come in an even number, so they leave the view on tag 2 and the
# MOD+1 behind them is what makes the window viewable again.
kill -STOP "$WM_PID"
xdotool key --delay 0 --clearmodifiers "${keys[@]}" super+1 2>/dev/null
kill -CONT "$WM_PID"

start=$(now_ms)
elapsed=$BUDGET_MS
while [ $(( $(now_ms) - start )) -lt "$BUDGET_MS" ]; do
  if [ "$(count_viewable)" -eq 1 ]; then
    elapsed=$(($(now_ms) - start))
    break
  fi
  sleep 0.01
done

if [ "$elapsed" -ge "$BUDGET_MS" ]; then
  fail "the MOD+1 queued behind $BURST key presses was not answered within ${BUDGET_MS} ms"
else
  pass "the queued MOD+1 was answered after ${elapsed} ms of backlog"
fi
check_wm_alive

finish
