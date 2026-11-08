#!/usr/bin/env bash
# wait_viewable 0 used to return success before it looked at anything, because
# its loop asked whether the viewable count was at least 0. A seam test that
# waits for a tag switch to empty the screen built on that read a pass while
# every window was still on screen; test-60-ewmh-root-properties.sh carried an
# inline loop instead. The helper refuses a count below 1 now, including a
# zero written as 00, and wait_hidden is what a test calls to wait for no window.
source "$(dirname "$0")/lib.sh"

# A count below 1 stops the file that asks, so the refusal runs as its own
# process: the caller's success line is what a silent at-once return would print.
DIR="$TMPDIR_SEAM/wait-helpers"
rm -rf "$DIR"
mkdir -p "$DIR"

cat >"$DIR/waits-for-none.sh" <<'EOF'
#!/usr/bin/env bash
source "$LIB_SH"
if wait_viewable 0 2000; then
  pass "wait_viewable 0 returned success although nothing was waited for"
fi
finish
EOF

OUT="$DIR/out-none"
LIB_SH="$ROOT_DIR/tests/x/lib.sh" bash "$DIR/waits-for-none.sh" >"$OUT" 2>&1
rc=$?
check_eq "wait_viewable 0 fails the file that asks" 1 "$rc"
check_eq "the refusal names the helper and says what to use instead" 1 \
  "$(grep -c "^  FAIL  wait_viewable: .*Use wait_hidden" "$OUT")"
check_eq "the caller's success line never runs" 0 \
  "$(grep -c 'returned success although nothing was waited for' "$OUT")"
check_eq "the refused file still prints its own count" 1 \
  "$(grep -c '^  0 passed, 1 failed$' "$OUT")"

# "00" is zero to the comparison and is not the spelling "0". The string
# check used to accept it, and the helper returned success before it looked.
cat >"$DIR/waits-for-padded-zero.sh" <<'EOF'
#!/usr/bin/env bash
source "$LIB_SH"
if wait_viewable 00 2000; then
  pass "wait_viewable 00 returned success although nothing was waited for"
fi
finish
EOF

OUT_PAD="$DIR/out-padded"
LIB_SH="$ROOT_DIR/tests/x/lib.sh" bash "$DIR/waits-for-padded-zero.sh" >"$OUT_PAD" 2>&1
rc_pad=$?
check_eq "wait_viewable 00 fails the file that asks" 1 "$rc_pad"
check_eq "a padded zero is refused as not a positive count" 1 \
  "$(grep -c "^  FAIL  wait_viewable: '00' " "$OUT_PAD")"
check_eq "a padded zero does not return success" 0 \
  "$(grep -c 'returned success although nothing was waited for' "$OUT_PAD")"

# The same refusal with the caller's redirect in force. A caller that discards
# the helper's stdout must still leave a FAIL line, because run.sh counts those
# lines and not the exit status.
cat >"$DIR/waits-for-none-discarded.sh" <<'EOF'
#!/usr/bin/env bash
source "$LIB_SH"
wait_viewable 0 2000 >/dev/null
pass "the refusal was discarded with the redirect"
finish
EOF

OUT_DISCARD="$DIR/out-none-discarded"
LIB_SH="$ROOT_DIR/tests/x/lib.sh" bash "$DIR/waits-for-none-discarded.sh" >"$OUT_DISCARD" 2>&1
check_eq "a redirect does not swallow the refusal" 1 \
  "$(grep -c '^  FAIL  wait_viewable: ' "$OUT_DISCARD")"
check_eq "the caller never reached its next line" 0 \
  "$(grep -c 'the refusal was discarded with the redirect' "$OUT_DISCARD")"

mapfile -t ids < <(spawn_client 1 400 300 wait-helpers)
require_visible 1 "the window is on screen before waiting for none"

# A window that is still there is the state wait_hidden has to keep waiting in;
# returning success here is the wrong-reason pass this helper exists to remove.
if wait_hidden 200; then
  fail "wait_hidden returned while the window was still viewable ($(count_viewable) viewable)"
else
  pass "wait_hidden keeps waiting while a window is viewable"
fi

kill "$(last_client_pid)" 2>/dev/null
if wait_hidden 2000; then
  pass "wait_hidden returns once no window is viewable"
else
  fail "wait_hidden timed out with $(count_viewable) window(s) viewable after the client exited"
fi

finish
