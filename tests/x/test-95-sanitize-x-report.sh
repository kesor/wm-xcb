#!/usr/bin/env bash
# scripts/check-sanitize-x.sh can be made green without checking anything: a run
# of an uninstrumented binary leaves a log with no report in it, and a
# suppression file silences a report that the code still produces. Both look
# like a clean run in the log.
#
# The judgement lives in scripts/lib/sanitize-x-report.sh, so this test feeds it
# recorded harness output instead of starting a nested server of its own. What
# it cannot test here is that the harness line comes from a real run; that is
# what the gate does in CI.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/sanitize-x-report.sh
. "$ROOT_DIR/scripts/lib/sanitize-x-report.sh"

DIR="$TMPDIR_SEAM/sanitize-x-report"
rm -rf "$DIR"
mkdir -p "$DIR"

LEAK_LOG="$DIR/leak.log"
cat >"$LEAK_LOG" <<EOF
Direct leak of 8 byte(s) in 1 object(s) allocated from:
    #0 0x7ffff792c01f in malloc (/nix/store/asan/lib/libasan.so.8+0x12c01f)
    #1 0x5555555a23aa in setup_ewmh $ROOT_DIR/wm-xcb-ewmh.c:19
    #2 0x55555559ba20 in main $ROOT_DIR/wm.c:23

SUMMARY: AddressSanitizer: 8 byte(s) leaked in 1 allocation(s).
EOF

CLEAN_LOG="$DIR/clean.log"
cat >"$CLEAN_LOG" <<EOF
DEBUG: starting the WM
DEBUG: connected to the X server
EOF

UBSAN_LOG="$DIR/ubsan.log"
cat >"$UBSAN_LOG" <<EOF
$ROOT_DIR/wm-xcb.c:212:9: runtime error: load of misaligned address
EOF

FATAL_LOG="$DIR/fatal.log"
cat >"$FATAL_LOG" <<EOF
==1==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7f
    #0 0x7ffff792c01f in memset (/nix/store/asan/lib/libasan.so.8+0x12c01f)
EOF

# A harness line names the variant, the binary and the log. Only the variant and
# the log matter to the verdict.
harness_line() { # harness_line <variant> <log>
  echo "xephyr-test.sh: variant=$1 (DEBUG=0 SANITIZE=1), binary=$ROOT_DIR/build/$1/wm, log=$2"
}

BASELINE="$DIR/baseline-with-leak.txt"
{
  echo "# A known leak site."
  echo "wm-xcb-ewmh.c|setup_ewmh"
} >"$BASELINE"
EMPTY_BASELINE="$DIR/baseline-empty.txt"
echo "# Nothing known." >"$EMPTY_BASELINE"

LAST="$DIR/last.txt"
expect_verdict() { # <what> <expected-status> <variant> <log> <baseline>
  local out="$DIR/harness-$RANDOM.out" got
  harness_line "$3" "$4" >"$out"
  sanitize_x_verify "$out" "$5" >"$LAST" 2>&1
  got=$?
  check_eq "$1: exit status" "$2" "$got"
}

expect_verdict "a baselined leak is accepted" 0 release-sanitize "$LEAK_LOG" "$BASELINE"
check_eq "a known site is not reported as new" 0 "$(grep -c 'NEW REPORT' "$LAST")"

expect_verdict "a leak missing from the baseline fails" 1 release-sanitize "$LEAK_LOG" "$EMPTY_BASELINE"
check_eq "the new site is named" 1 \
  "$(grep -c 'NEW REPORT  wm-xcb-ewmh.c|setup_ewmh' "$LAST")"

expect_verdict "an uninstrumented variant fails" 1 release "$LEAK_LOG" "$BASELINE"
check_eq "the refusal names the variant it saw" 1 \
  "$(grep -c "variant='release', not release-sanitize" "$LAST")"

expect_verdict "a fatal ASan error fails" 1 release-sanitize "$FATAL_LOG" "$BASELINE"
check_eq "the fatal report is echoed" 1 \
  "$(grep -c 'ERROR: AddressSanitizer: stack-buffer-overflow' "$LAST")"

expect_verdict "no report next to a baseline entry fails" 1 release-sanitize "$CLEAN_LOG" "$BASELINE"
check_eq "the refusal says the baseline expects a site" 1 \
  "$(grep -c 'lists 1 site' "$LAST")"

expect_verdict "no report next to an empty baseline is accepted" 0 release-sanitize "$CLEAN_LOG" "$EMPTY_BASELINE"
check_eq "an empty log and an empty baseline is the one clean case" 1 \
  "$(grep -c 'no sanitizer report at all' "$LAST")"

expect_verdict "a UBSan report is a site" 1 release-sanitize "$UBSAN_LOG" "$EMPTY_BASELINE"
check_eq "the UBSan site is keyed by file and message" 1 \
  "$(grep -c 'NEW REPORT  wm-xcb.c|runtime error: load of misaligned address' "$LAST")"

# A harness line with no log at all cannot be judged.
NO_LOG="$DIR/harness-no-log.out"
echo "xephyr-test.sh: variant=release-sanitize (DEBUG=0 SANITIZE=1), binary=$ROOT_DIR/build/release-sanitize/wm, log=$DIR/missing.log" >"$NO_LOG"
sanitize_x_verify "$NO_LOG" "$BASELINE" >"$LAST" 2>&1
check_eq "a harness line naming no readable log fails" 1 "$?"

# The environment cannot decide what the gate sees. Both keys that turn a report
# into silence are dropped, and a key the caller legitimately set survives.
ASAN_OPTIONS="suppressions=$DIR/ignore.txt,detect_leaks=1,log_path=$DIR/asan"
LSAN_OPTIONS="leak_check_at_exit=1,suppressions=$DIR/ignore.txt"
UBSAN_OPTIONS="print_stacktrace=1"
sanitize_x_scrub_options ASAN_OPTIONS
sanitize_x_scrub_options LSAN_OPTIONS
sanitize_x_scrub_options UBSAN_OPTIONS
check_eq "suppressions= is dropped from ASAN_OPTIONS" "detect_leaks=1" "$ASAN_OPTIONS"
check_eq "log_path= is dropped from ASAN_OPTIONS" 0 \
  "$(printf '%s' "$ASAN_OPTIONS" | grep -c 'log_path')"
check_eq "suppressions= is dropped from LSAN_OPTIONS" "leak_check_at_exit=1" "$LSAN_OPTIONS"
check_eq "other options survive the scrub" "print_stacktrace=1" "$UBSAN_OPTIONS"

# The runtime also splits on colon and whitespace. A comma-only scrub leaves
# suppressions= in place when another key precedes it, and the report stays hidden.
ASAN_OPTIONS="halt_on_error=0:suppressions=$DIR/ignore.txt:log_path=$DIR/asan"
LSAN_OPTIONS="print_suppressions=0 suppressions=$DIR/ignore.txt"
sanitize_x_scrub_options ASAN_OPTIONS
sanitize_x_scrub_options LSAN_OPTIONS
check_eq "colon-separated suppressions= and log_path= are dropped" "halt_on_error=0" "$ASAN_OPTIONS"
check_eq "space-separated suppressions= is dropped" "print_suppressions=0" "$LSAN_OPTIONS"

UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
sanitize_x_scrub_options UBSAN_OPTIONS
check_eq "a colon-separated option that is not dropped survives" \
  "print_stacktrace=1,halt_on_error=1" "$UBSAN_OPTIONS"

# The gate runs under set -u and calls the scrub on variables the caller never set.
unset ASAN_OPTIONS
set -u
sanitize_x_scrub_options ASAN_OPTIONS
set +u
check_eq "an unset option string scrubs to empty" "" "${ASAN_OPTIONS-}"

finish
