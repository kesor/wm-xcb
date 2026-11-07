#!/usr/bin/env bash
#
# test-116-perf-parent-display.sh - a performance run draws into a display it
# owns.
#
# scripts/perf-test.sh used to start Xephyr without naming a parent display, so
# the nested server drew into whatever DISPLAY said. A measurement holds that
# display for seconds at a time and takes the input focus with -noreset, so a
# run interrupted the person who started it, and the numbers depended on the
# depth and health of a display the run did not own.
#
# It now gets a parent display from scripts/lib/private-display.sh, like the
# other harnesses. This asserts the line that names it points at the suite's
# private parent and not at the display the suite is testing, and watches that
# display for a Xephyr window while the run is in progress, the way test-90
# checks xephyr-test.sh. The key is that a run which inherited DISPLAY would put
# its nested server on the display under test, as a client of this suite's WM.
#
# The session's display is not searched for Xephyr windows: xvfb-run replaces
# XAUTHORITY for the private server, so a test inside it cannot authenticate to
# the display the caller sat on.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

OUT="$TMPDIR_SEAM/perf-parent-display.out"
# The variant is pinned rather than inherited: `make SANITIZE=1 test-x` exports
# SANITIZE to every test, and an inherited one builds and reports a variant the
# assertions below do not name. The run waits 2 s for the idle number and 1 s
# for the WM to start; two minutes is a stuck run, not a slow one.
timeout 120 env -u SANITIZE DEBUG=0 "$ROOT_DIR/scripts/perf-test.sh" --windows 1 >"$OUT" 2>&1 &
run_pid=$!

# The display the suite is testing is the one an inherited DISPLAY points a
# nested server at, so a Xephyr there would be a client of the WM under test:
# that is the defect, watched while the run is in progress rather than inferred
# from the line it printed.
drawn_on_under_test=0
while kill -0 "$run_pid" 2>/dev/null; do
  if [ -n "$(DISPLAY="$DISPLAY" xdotool search --class Xephyr 2>/dev/null)" ]; then
    drawn_on_under_test=1
    break
  fi
  sleep 0.05
done
wait "$run_pid"
rc=$?

check_eq "perf-test.sh exits 0" 0 "$rc"
check_eq "no Xephyr window appeared on the display under test while it ran" 0 "$drawn_on_under_test"
check_eq "it builds and names the release variant" 1 \
  "$(grep -c '^perf-test.sh: variant=release ' "$OUT")"

PARENT=$(sed -n 's/^perf-test.sh: parent display = \([^ ]*\) .*/\1/p' "$OUT" | head -n 1)
check_nonempty "it names the parent display it drew into" "$PARENT"
check_eq "the parent display is the suite's private parent" "$WM_PARENT_DISPLAY" "$PARENT"
if [ -n "$PARENT" ] && [ "$PARENT" = "$DISPLAY" ]; then
  fail "it nested into $PARENT, the display under test; its Xephyr would be a client of this suite's WM"
else
  pass "it does not draw into the display under test ($DISPLAY)"
fi

NESTED=$(sed -n 's/^nested display: \(:[0-9]*\) .*/\1/p' "$OUT" | head -n 1)
check_nonempty "it says which display the WM ran on" "$NESTED"
if [ -n "$NESTED" ] && [ "$NESTED" != "$DISPLAY" ] && [ "$NESTED" != "$PARENT" ]; then
  pass "the server under test ($NESTED) is neither the parent nor the suite's display"
else
  fail "it ran the WM on $NESTED, which is the suite's display or its parent"
fi

if [ "$rc" -ne 0 ]; then
  sed 's/^/    /' "$OUT"
fi

finish
