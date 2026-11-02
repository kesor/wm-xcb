#!/usr/bin/env bash
#
# check-sanitize-x.sh - run the WM in a nested X server under ASan and UBSan.
#
# Usage: scripts/check-sanitize-x.sh [--update-baseline] [--verbose]
#
# scripts/check-sanitize.sh runs the unit suite under the sanitizers, and that
# suite has no X connection: the component code that talks to X returns early
# when there is no display, so the unit run never builds a request. Anything
# wrong with a request therefore cannot appear there. Mapping a window read 4
# bytes out of a 2-byte border width, survived the whole unit suite and every
# other gate, and aborted on the first MapRequest under ASan.
#
# So this gate starts a real WM against a nested server with
# scripts/xephyr-test.sh and fails on three things: the harness failing (the WM
# died, a window was never mapped, windows do not tile), an ASan error in the
# WM log, and a leak site or UBSan report in that log that is not listed in
# tests/quality/sanitize-x-baseline.txt.
#
# Leaks are baselined rather than fatal because the harness stops the WM with a
# signal, so LeakSanitizer reports whatever was still allocated at that moment,
# startup included. A site that is not in the baseline is new, which is the
# point: the list only shrinks. Fixing a baselined site means updating the
# baseline in the same commit, which is the policy the other two baselines
# follow.
#
# Silence is not a verdict, so the gate refuses three ways of producing it.
# The run must report variant=release-sanitize: an uninstrumented binary writes
# a log with no report in it, and no line in that log says the sanitizer never
# started. suppressions= and log_path= are dropped from ASAN_OPTIONS,
# LSAN_OPTIONS and UBSAN_OPTIONS before the harness starts, so a caller cannot
# hide a report with an environment variable or a suppression file. And a
# baseline with entries next to a log with no report fails: the fixes landed
# without their baseline update, or the sanitizer did not run, and the log
# cannot say which. The harness line is judged in
# scripts/lib/sanitize-x-report.sh. The scrub and the site verdict live in
# scripts/lib/sanitize-report.sh, which the unit gate reads too.
#
# Needs what the harness needs: Xephyr, xdotool, xprop, and a parent display for
# Xephyr to draw into. That display is a private Xvfb, started here through
# scripts/lib/private-display.sh and handed to the harness, so the gate draws
# nothing on the session. CI needs no display of its own for the same reason; it
# used to wrap this script in xvfb-run.
#
# Exit status: 0 when the run is clean, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BASELINE="$ROOT_DIR/tests/quality/sanitize-x-baseline.txt"
UPDATE=0
VERBOSE=0
CHECK_ARGS=("$@")
for arg in "$@"; do
  case "$arg" in
    --update-baseline) UPDATE=1 ;;
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *)
      echo "check-sanitize-x: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

export SANITIZE_VERBOSE=$VERBOSE
# shellcheck source=lib/sanitize-x-report.sh
. "$ROOT_DIR/scripts/lib/sanitize-x-report.sh"
# shellcheck source=lib/private-display.sh
. "$ROOT_DIR/scripts/lib/private-display.sh"

# Before anything starts: the helper re-executes this script under xvfb-run, and
# the harness it runs later inherits that display rather than starting a second
# one. The arguments are the ones this invocation was given, not what the loop
# above left of them.
wm_parent_display "${CHECK_ARGS[@]}" || exit 1

for option_var in ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS; do
  sanitize_scrub_options "$option_var"
done

# The leak report is the exit-time one, so it is only complete if the harness
# lets the WM exit rather than killing it outright. The option strings are read
# left to right and the last value of a repeated key wins, so these two keys are
# appended after the scrub and beat anything the caller left. LeakSanitizer
# options override AddressSanitizer's leak flags, which is why the same keys are
# forced on LSAN_OPTIONS and not only on ASAN_OPTIONS.
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS,}detect_leaks=1,leak_check_at_exit=1"
export LSAN_OPTIONS="${LSAN_OPTIONS:+$LSAN_OPTIONS,}detect_leaks=1,leak_check_at_exit=1"

# The harness refuses to run without the test client, and `make clean` in CI
# removes it.
make -s tools/xclient >/dev/null || {
  echo "check-sanitize-x: cannot build tools/xclient" >&2
  exit 1
}

OUT=$(mktemp) || exit 1
trap 'rm -f "$OUT"' EXIT

echo "check-sanitize-x: running scripts/xephyr-test.sh against the SANITIZE=1 build"
# nix develop sets TMPDIR to a private directory. The harness writes its log
# under $TMPDIR, and the CI upload looks at /tmp/wm-xephyr-test.
# DEBUG is dropped rather than forwarded: the gate says which variant it ran,
# and debug-sanitize would make that line depend on the caller's environment.
env -u DEBUG TMPDIR=/tmp SANITIZE=1 "$ROOT_DIR/scripts/xephyr-test.sh" >"$OUT" 2>&1
rc=$?
cat "$OUT"
if [ "$rc" -ne 0 ]; then
  echo "check-sanitize-x: the harness failed; the WM does not survive a sanitized run against X"
  exit 1
fi

# The verdict needs the log the harness wrote, not a log this script guessed.
LOG=$(sanitize_x_trust "$OUT") || exit 1
mapfile -t SITES < <(sanitize_report_sites "$LOG")

if [ "$UPDATE" -eq 1 ]; then
  sanitize_write_baseline "$BASELINE" "scripts/check-sanitize-x.sh --update-baseline" "${SITES[@]}"
  echo "check-sanitize-x: baseline updated with ${#SITES[@]} site(s)"
  exit 0
fi

sanitize_x_verify "$OUT" "$BASELINE"
