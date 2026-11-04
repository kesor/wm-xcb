#!/usr/bin/env bash
# tests/x/probe.sh answers "what does the WM do here?" in one call, so a defect
# nobody has written a test for yet does not cost a hand-written script in /tmp.
# A tool that prints the wrong display, the wrong variant, or a table with a
# column missing would be trusted further than a probe script was, because it
# lives in the repository. These assertions pin what the shape promises: the
# header, one row per window with the state read from X, the command section,
# and the WM log. The last two pin the exit status, which is how a caller can
# tell a run that worked from a command that failed.
source "$(dirname "$0")/lib.sh"

OUT="$TMPDIR_SEAM/probe.out"
# The variant is pinned rather than inherited: the probe builds the WM it runs,
# and `make SANITIZE=1 test-x` exports SANITIZE to every test, which would build
# a variant the assertions below do not name. The probe starts its own nested
# server and waits 200 ms twice; two minutes is a stuck run, not a slow one.
timeout 120 env -u SANITIZE DEBUG=0 \
  "$ROOT_DIR/tests/x/probe.sh" -n 2 -w 200 -c 'xdotool getdisplaygeometry' -l 40 \
  >"$OUT" 2>&1
rc=$?
printf '  probe output in %s\n' "$OUT"

check_eq "the probe exits 0" 0 "$rc"
check_eq "it builds and names the release variant" 1 \
  "$(grep -c '^probe.sh: variant=release ' "$OUT")"

# run.sh has pointed DISPLAY at the run's own Xephyr, and its WM is the one
# under test. The probe must nest its server into the private parent instead, or
# its windows become clients of that WM and every row describes the wrong WM.
check_eq "the probe nests into the private parent display" "$WM_PARENT_DISPLAY" \
  "$(sed -n 's/^probe.sh: parent display = \([^ ]*\) .*/\1/p' "$OUT" | head -n 1)"
PROBE_DISPLAY=$(sed -n 's/^probe: display=\(:[0-9]*\) .*/\1/p' "$OUT" | head -n 1)
check_nonempty "it says which display it started the WM on" "$PROBE_DISPLAY"
if [ -n "$PROBE_DISPLAY" ] && [ "$PROBE_DISPLAY" != "$DISPLAY" ]; then
  pass "it does not take the display the seam run is using ($DISPLAY)"
else
  fail "it runs on $PROBE_DISPLAY, which is the display under test"
fi

check_eq "it reports the screen it started" 1 "$(grep -c '^screen: 1280 800$' "$OUT")"
check_eq "it prints a row per window" 2 \
  "$(sed -n '/^--- state ---$/,/^viewable: /p' "$OUT" | grep -c '^0x')"
check_eq "every row is a viewable client" 2 \
  "$(sed -n '/^--- state ---$/,/^viewable: /p' "$OUT" | awk '$1 ~ /^0x/ && $7 == "yes"' | grep -c .)"
check_eq "the viewable count matches the rows" 1 "$(grep -c '^viewable: 2 of 2$' "$OUT")"
check_eq "exactly one row is the focused window" 1 \
  "$(sed -n '/^--- state ---$/,/^viewable: /p' "$OUT" | awk '$1 ~ /^0x/ && $8 == "yes"' | grep -c .)"

# One line per column, whatever the value: a column that is empty because
# nothing reads it is a table that hides the question, not one that answers it.
check_eq "the header names every column" 1 \
  "$(grep -c '^window *x *y *w *h *border *viewable *focus *tags *title$' "$OUT")"
check_eq "every row carries a tags value" 0 \
  "$(sed -n '/^--- state ---$/,/^viewable: /p' "$OUT" | awk '$1 ~ /^0x/ && $9 == ""' | grep -c .)"

check_eq "the command ran against the probe's display" 1 \
  "$(grep -c '^1280 800$' "$OUT")"
check_eq "it reports the command's exit status" 1 "$(grep -c '^command: exit 0$' "$OUT")"

check_eq "it names the root properties the WM does not set" 5 \
  "$(sed -n '/^--- root properties ---$/,/^--- wm ---$/p' "$OUT" | grep -cE '^_NET_[A-Z_]+: ')"
check_eq "it reports the WM alive" 1 "$(grep -c '^wm: running (pid [0-9]*)$' "$OUT")"
check_eq "it prints the WM log" 1 "$(grep -c '^--- wm log (last 40 lines) ---$' "$OUT")"

# The probe's own exit status is the command's, so a caller can put an
# assertion inside -c and read the answer from the shell. Without this, a
# failing command in a successful probe reads as a passing one.
timeout 120 env -u SANITIZE DEBUG=0 \
  "$ROOT_DIR/tests/x/probe.sh" -n 0 -w 100 -c 'exit 7' >"$OUT" 2>&1
rc=$?
check_eq "a command that fails is the probe's exit status" 7 "$rc"
check_eq "it reports the failure too" 1 "$(grep -c '^command: exit 7$' "$OUT")"
check_eq "no windows is a run, not a failure" 1 "$(grep -c '^viewable: 0 of 0$' "$OUT")"

finish
