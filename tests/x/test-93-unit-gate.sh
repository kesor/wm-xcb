#!/usr/bin/env bash
# The commit gate ran the unit suite with its output attached, so every commit
# printed every passing assertion before lefthook's own summary line. A worker
# spent most of a session's context reading a step that passed, and the workaround
# (redirect the commit to a file, read the tail) was something each session had to
# discover again. This pins the gate's three verdicts without running the suite: a
# pass is the summary line alone, a failure carries the suite's own output, and a
# run with no summary line is not a pass however it exited.
#
# The debug gate runs the same script with DEBUG=1, so the last assertions pin the
# other half: the variant make answered with is on the pass line, and the suite is
# built with the DEBUG and SANITIZE the gate was given.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/unit-gate"
rm -rf "$DIR"
mkdir -p "$DIR/bin"

# The gate asks make which variant it builds, then runs the suite; this stands in
# for make so the assertions here are about the gate, not about the suite or the
# real variant name. The variant answer follows the same rule the Makefile uses.
cat >"$DIR/bin/make" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$STUB_ARGS"
case "$*" in
  *print-variant*)
    variant=release
    case "$*" in *DEBUG=1*) variant=debug ;; esac
    case "$*" in *SANITIZE=1*) variant="$variant-sanitize" ;; esac
    echo "$variant"
    ;;
  *)
    cat "$STUB_LOG"
    exit "$STUB_STATUS"
    ;;
esac
EOF
chmod +x "$DIR/bin/make"

# run_gate <name> <make-status> <log line>... - prints the gate's exit status.
# GATE_DEBUG and GATE_SANITIZE, when set, are handed to the gate as DEBUG and
# SANITIZE are on a real run.
run_gate() {
  local name=$1 status=$2 log args
  local -a gate_env
  log="$DIR/$name.log"
  args="$DIR/$name.args"
  shift 2
  printf '%s\n' "$@" >"$log"
  : >"$args"
  gate_env=(STUB_LOG="$log" STUB_STATUS="$status" STUB_ARGS="$args")
  if [ -n "${GATE_DEBUG:-}" ]; then gate_env+=(DEBUG="$GATE_DEBUG"); fi
  if [ -n "${GATE_SANITIZE:-}" ]; then gate_env+=(SANITIZE="$GATE_SANITIZE"); fi
  # `make SANITIZE=1 test-x` exports SANITIZE into every test. lib.sh unsets
  # DEBUG, because libxdo dumps its keymap when that name is set, and leaves
  # SANITIZE. An inherited 1 makes the stub answer release-sanitize, and the
  # release and debug pass-line assertions fail for a variant the case did not
  # ask for. -u drops both; a value this case set is applied after and is the
  # one the gate reads.
  env -u DEBUG -u SANITIZE "${gate_env[@]}" PATH="$DIR/bin:$PATH" \
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

check_eq "a passing run names the build variant make answered with" 1 \
  "$(grep -c 'variant=release ' "$DIR/pass.out")"

GATE_DEBUG=1
check_eq "the debug gate passes" 0 "$(run_gate debug 0 "${PASSING_LOG[@]}")"
GATE_DEBUG=
check_eq "the debug pass line names the debug variant" 1 \
  "$(grep -c 'variant=debug (DEBUG=1 SANITIZE=0)' "$DIR/debug.out")"
check_eq "the suite is built with the DEBUG the gate was given" 1 \
  "$(grep -cE '^test .*DEBUG=1' "$DIR/debug.args")"
check_eq "the variant question carries the same DEBUG" 1 \
  "$(grep -cE 'print-variant .*DEBUG=1' "$DIR/debug.args")"

GATE_SANITIZE=1
check_eq "the sanitize gate passes" 0 "$(run_gate sanitize 0 "${PASSING_LOG[@]}")"
GATE_SANITIZE=
check_eq "its pass line names the sanitize variant" 1 \
  "$(grep -c 'variant=release-sanitize (DEBUG=0 SANITIZE=1)' "$DIR/sanitize.out")"
check_eq "the suite is built with the SANITIZE the gate was given" 1 \
  "$(grep -cE '^test .*SANITIZE=1' "$DIR/sanitize.args")"

# The -u above has to hold when the caller exported SANITIZE, which is what
# `make SANITIZE=1 test-x` does. export, not a shell assignment: run_gate
# hands the environment to env(1), which does not see an unexported variable,
# so a plain assignment would pass even with the -u deleted.
export SANITIZE=1
check_eq "an inherited SANITIZE still passes the release run" 0 \
  "$(run_gate inherited 0 "${PASSING_LOG[@]}")"
unset SANITIZE
check_eq "an inherited SANITIZE does not relabel the release pass" 1 \
  "$(grep -c 'variant=release (DEBUG=0 SANITIZE=0)' "$DIR/inherited.out")"

finish
