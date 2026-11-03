#!/usr/bin/env bash
#
# test-94-fresh-wm-per-file.sh - the WM this file talks to started with it.
#
# run.sh keeps one Xephyr for the run and gives every test file a WM of its own,
# because a tag view, a client moved off the visible tag, the input focus and the
# layout all outlive the file that set them. A file that inherits that state
# fails on something it never did, and the failure names the wrong test.
#
# The age of the WM comes from its own /proc entry, so the assertion holds when
# this file runs on its own: a filtered run starts a WM too, and there is then
# nothing to inherit.
#
# run.sh has already started the WM and exported WM_PID, WM_LOG and ROOT_DIR.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

# The harness waits at most 4 s for the WM to select SubstructureRedirect on the
# root before it reports a startup failure, so a WM that answered at all is
# younger than that. 5 s leaves the file's own start in the window.
FRESH_MS=5000
CLK_TCK="$(getconf CLK_TCK 2>/dev/null || echo 100)"

if [ ! -r "/proc/$WM_PID/stat" ]; then
  fail "WM start time readable: /proc/$WM_PID/stat is unreadable"
  finish
  exit 1
fi

# Field 22 of /proc/<pid>/stat is the process's start time in clock ticks since
# boot; /proc/uptime is the boot clock in seconds.
wm_age_ms() {
  awk -v hz="$CLK_TCK" 'NR == FNR { start = $22; next } { printf "%d", ($1 - start / hz) * 1000 }' \
    "/proc/$WM_PID/stat" /proc/uptime
}

check_wm_alive
check_le "the age of the WM serving this file (ms)" "$FRESH_MS" "$(wm_age_ms)"

finish
