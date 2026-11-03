#!/usr/bin/env bash
#
# check-sanitize.sh - fail on a sanitizer report that is not already known.
#
# Usage: scripts/check-sanitize.sh [--update-baseline] [--verbose]
#
# The unit suite runs under AddressSanitizer and UndefinedBehaviorSanitizer.
# Leaks are reported with the allocation site: the first stack frame that is one
# of this repository's files. A site listed in tests/quality/asan-baseline.txt is
# known debt; a site that is not listed fails the check.
#
# Why a baseline rather than a clean run: the tag-manager and tiling test groups
# only started running recently, and they leaked machines the monitor never
# destroyed. A gate that is red for reasons nobody is fixing today teaches people
# to ignore it. The baseline makes the number go down and nothing else.
#
# Silence is not a verdict, so the gate refuses the same shapes the nested-server
# gate refuses, through the same library (scripts/lib/sanitize-report.sh):
# suppressions= and log_path= are dropped from the caller's sanitizer options, a
# baseline with entries next to a log with no report fails, and the test binary
# has to carry the ASan runtime. That last one is this gate's version of the
# harness line the other gate reads: make SANITIZE=1 is this script's own
# choice, but a tree where the flag stopped reaching the compiler produces a
# clean log and no line in it says so.
#
# Exit status: 0 when there is no new leak site, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BASELINE="$ROOT_DIR/tests/quality/asan-baseline.txt"
UPDATE=0
VERBOSE=0
for arg in "$@"; do
  case "$arg" in
    --update-baseline) UPDATE=1 ;;
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *)
      echo "check-sanitize: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

export SANITIZE_VERBOSE=$VERBOSE
# shellcheck source=lib/sanitize-report.sh
. "$ROOT_DIR/scripts/lib/sanitize-report.sh"

for option_var in ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS; do
  sanitize_scrub_options "$option_var"
done
# The leak report is the exit-time one, so the run has to ask for it. The option
# strings are read left to right and the last value of a repeated key wins, so
# these are appended after the scrub and beat anything the caller left.
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS,}detect_leaks=1,leak_check_at_exit=1"
export LSAN_OPTIONS="${LSAN_OPTIONS:+$LSAN_OPTIONS,}detect_leaks=1,leak_check_at_exit=1"

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

VARIANT=$(make -s print-variant SANITIZE=1) || {
  echo "check-sanitize: cannot read the build variant from make" >&2
  exit 1
}
TEST_BIN="$ROOT_DIR/build/$VARIANT/test"

echo "check-sanitize: building variant=$VARIANT and running the suite"
make clean >/dev/null 2>&1
# A leak report makes the test binary exit non-zero, which is not the same thing
# as a failed assertion: parse the summary and decide from that.
make SANITIZE=1 test >"$LOG" 2>&1 || true

# The summary proves the suite ran to the end. The ASan symbol in the binary
# proves it ran instrumented; the summary below is what a report looks like, and
# a run with no report in it looks the same whether the sanitizer found nothing
# or never started.
if ! grep -qE 'Total: [0-9]+ passed, 0 failed' "$LOG"; then
  grep -E "error:" "$LOG" | head -20 >&2
  echo "check-sanitize: the tree does not build or the suite failed; see below" >&2
  tail -30 "$LOG" >&2
  exit 1
fi
if [ ! -x "$TEST_BIN" ]; then
  echo "check-sanitize: make reported variant=$VARIANT but $TEST_BIN is missing" >&2
  exit 1
fi
# grep -c, not grep -q: -q exits at the first match and the SIGPIPE reaches nm,
# which under pipefail turns a found symbol into a failed pipeline.
if [ "$(nm "$TEST_BIN" 2>/dev/null | grep -c '__asan_init' || true)" -eq 0 ]; then
  echo "check-sanitize: $TEST_BIN carries no ASan runtime, so SANITIZE=1 did not reach the compiler." >&2
  echo "check-sanitize: an uninstrumented suite writes a log with no report in it, which reads as a clean run." >&2
  exit 1
fi

mapfile -t SITES < <(sanitize_report_sites "$LOG")

if [ "$UPDATE" -eq 1 ]; then
  sanitize_write_baseline "$BASELINE" "scripts/check-sanitize.sh --update-baseline" "${SITES[@]}"
  echo "check-sanitize: baseline updated with ${#SITES[@]} site(s)"
  exit 0
fi

sanitize_verify_sites "$LOG" "$BASELINE" check-sanitize
