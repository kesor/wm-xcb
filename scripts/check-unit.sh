#!/usr/bin/env bash
#
# check-unit.sh - the unit suite as the commit and push gates run it.
#
# Usage: [DEBUG=1] [SANITIZE=1] scripts/check-unit.sh
#
# The runner prints one line per assertion, so a passing `make test` is a wall of
# green ending in the summary line lefthook already prints with the duration.
# On failure the same log is the first thing a person needs, because the failing
# assertion and the compiler error are both in it. The gate therefore captures
# the run and prints the log only when the run fails; a pass prints the summary
# line alone.
#
# The exit status of make is not the verdict by itself. A suite that never ran
# looks exactly like a suite that passed when something between here and the
# runner swallows the status, so the summary line has to be in the log and has
# to say zero failed, or the gate fails and prints the log it read.
#
# DEBUG and SANITIZE mean what they mean on the make command line, which is why
# the -pedantic debug gate runs its suite through this script rather than
# redirecting `make test` into /dev/null: a failing assertion in the debug build
# is what that gate is for, and the redirect kept make's "Error 1" line while
# dropping the assertion that failed. The pass line names the variant make
# answered with and the suite is built with the same two values, so a gate that
# asked for one variant and tested another cannot read as a pass.
#
# Exit status: 0 when the suite ran to the end with no failure, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

DEBUG=${DEBUG:-0}
SANITIZE=${SANITIZE:-0}

VARIANT=$(make -s print-variant DEBUG="$DEBUG" SANITIZE="$SANITIZE") || {
  echo "check-unit: cannot read the build variant from make" >&2
  exit 1
}
if [ -z "$VARIANT" ]; then
  echo "check-unit: make printed no build variant for DEBUG=$DEBUG SANITIZE=$SANITIZE" >&2
  exit 1
fi

LOG=$(mktemp) || {
  echo "check-unit: cannot create a log file" >&2
  exit 1
}
trap 'rm -f "$LOG"' EXIT

if ! make test DEBUG="$DEBUG" SANITIZE="$SANITIZE" >"$LOG" 2>&1; then
  cat "$LOG" >&2
  echo "check-unit: make test failed for variant=$VARIANT (DEBUG=$DEBUG SANITIZE=$SANITIZE); the suite's own output is above." >&2
  exit 1
fi

SUMMARY=$(grep -E '^[[:space:]]*Total: [0-9]+ passed, [0-9]+ failed[[:space:]]*$' "$LOG" | tail -1)
if [ -z "$SUMMARY" ]; then
  echo "check-unit: make test exited 0 and printed no 'Total: N passed, M failed' line." >&2
  echo "check-unit: a suite that did not run to the end cannot pass by exiting 0; the log follows." >&2
  cat "$LOG" >&2
  exit 1
fi

FAILED=${SUMMARY##* passed, }
FAILED=${FAILED%% failed*}
if [ "$FAILED" -ne 0 ]; then
  cat "$LOG" >&2
  echo "check-unit: the suite reported $FAILED failures and exited 0." >&2
  exit 1
fi

echo "check-unit: variant=$VARIANT (DEBUG=$DEBUG SANITIZE=$SANITIZE): $(printf '%s\n' "$SUMMARY" | sed -E 's/^[[:space:]]+//')"
