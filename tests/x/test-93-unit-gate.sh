#!/usr/bin/env bash
# The commit gate ran the unit suite with its output attached, so every commit
# printed every passing assertion before lefthook's own summary line. A worker
# spent most of a session's context reading a step that passed, and the workaround
# (redirect the commit to a file, read the tail) was something each session had to
# discover again. This pins the gate's three verdicts without running the suite: a
# pass is the summary line alone, a failure carries the suite's own output, and a
# run with no summary line is not a pass however it exited.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/unit-gate"
rm -rf "$DIR"
mkdir -p "$DIR/bin"

# The gate runs `make test`; this stands in for it so the assertion here is about
# the gate's verdict, not about the suite.
cat >"$DIR/bin/make" <<'EOF'
#!/usr/bin/env bash
cat "$STUB_LOG"
exit "$STUB_STATUS"
EOF
chmod +x "$DIR/bin/make"

# run_gate <name> <make-status> <log line>... - prints the gate's exit status.
run_gate() {
  local name=$1 status=$2 log
  log="$DIR/$name.log"
  shift 2
  printf '%s\n' "$@" >"$log"
  STUB_LOG="$log" STUB_STATUS="$status" PATH="$DIR/bin:$PATH" \
    "$ROOT_DIR/scripts/check-unit.sh" >"$DIR/$name.out" 2>&1
  echo $?
}

PASSING_LOG=(
  'tests/unit/test-wm-tag.c:111: tag->count == 2 - pass'
  '  [PASS] 1123 passed, 0 failed'
  ''
  '=== Test Results ==='
  '  Total: 1123 passed, 0 failed'
)

check_eq "a passing suite passes the gate" 0 "$(run_gate pass 0 "${PASSING_LOG[@]}")"
check_eq "a passing run prints one line" 1 "$(wc -l <"$DIR/pass.out")"
check_eq "that line is the suite's verdict" 1 "$(grep -c 'Total: 1123 passed, 0 failed' "$DIR/pass.out")"
check_eq "the assertion noise stays out of it" 0 "$(grep -c 'tag->count == 2' "$DIR/pass.out")"

check_eq "a failing suite fails the gate" 1 \
  "$(run_gate fail 1 "${PASSING_LOG[@]:0:2}" 'tests/unit/test-wm-tag.c:111: tag->count == 3 - FAIL' '  Total: 1122 passed, 1 failed')"
check_eq "the failure carries the failing assertion" 1 \
  "$(grep -c 'tag->count == 3 - FAIL' "$DIR/fail.out")"
check_eq "the failure names why the gate stopped" 1 "$(grep -c 'make test failed' "$DIR/fail.out")"
check_eq "the failure output is not one line" 1 \
  "$([ "$(wc -l <"$DIR/fail.out")" -gt 1 ] && echo 1 || echo 0)"

check_eq "a run with no summary line fails the gate" 1 \
  "$(run_gate nosummary 0 'make: nothing to be done for test')"
check_eq "it says the suite did not reach the end" 1 "$(grep -c 'did not run to the end' "$DIR/nosummary.out")"

check_eq "a summary with failures fails the gate even when make exits 0" 1 \
  "$(run_gate swallowed 0 '  Total: 1 passed, 2 failed')"
check_eq "it reports the count the summary carries" 1 "$(grep -c 'reported 2 failures' "$DIR/swallowed.out")"

finish
