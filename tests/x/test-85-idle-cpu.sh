#!/usr/bin/env bash
#
# test-85-idle-cpu.sh - the WM costs no CPU while nothing happens.
#
# A loop that asks "any events?" and gets "no" a million times a second spends a
# whole core on nothing. That reads as heat and battery drain long before it
# reads as a bug, so it is measured here: CPU time from /proc over a wall-clock
# window in which nothing is happening, with the windows the earlier tests left
# on screen.
#
# CPU time is Linux-only. Where /proc has no stat for the WM, the test fails
# rather than passing because it measured nothing.
#
# run.sh has already started the WM and exported WM_PID, WM_LOG and ROOT_DIR.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

# A spinning loop costs the whole window, so anything near the budget is a
# defect. The budget is not 0 because /proc counts in clock ticks (10 ms here)
# and this test starts while the previous test's windows are still being
# destroyed, which is work the WM is right to do.
IDLE_BUDGET_MS=50
IDLE_WINDOW_SECONDS=2
SETTLE_SECONDS=1
CLK_TCK="$(getconf CLK_TCK 2>/dev/null || echo 100)"

if [ ! -r "/proc/$WM_PID/stat" ]; then
  fail "idle CPU measurable: /proc/$WM_PID/stat is unreadable"
  finish
  exit 1
fi

wm_cpu_ms() {
  awk -v hz="$CLK_TCK" '{printf "%d", ($14 + $15) * 1000 / hz}' "/proc/$WM_PID/stat"
}

sleep "$SETTLE_SECONDS"
before=$(wm_cpu_ms)
sleep "$IDLE_WINDOW_SECONDS"
after=$(wm_cpu_ms)

check_le "idle CPU over $((IDLE_WINDOW_SECONDS * 1000)) ms wall (ms)" "$IDLE_BUDGET_MS" "$((after - before))"
check_wm_alive

finish
