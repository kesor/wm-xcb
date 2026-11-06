#!/usr/bin/env bash
#
# test-114-run-sh-refuses-empty-filter.sh - a --filter that matches no test file
# fails instead of reporting a green run that judged nothing.
#
# Every other way of naming no work in run.sh fails loudly: an unknown argument,
# a missing tool, a missing build product. A mistyped filter, or one naming a
# file that was renamed, used to print "0 file(s)" and exit 0, which reads as a
# pass. The refusal names the pattern and the glob it was matched against, so the
# next reader can tell a typo from an empty tests directory.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/empty-filter"
OUT="$TMPDIR_SEAM/empty-filter.out"
mkdir -p "$DIR"

timeout 120 env TMPDIR="$DIR" "$ROOT_DIR/tests/x/run.sh" \
  --filter=no-test-file-by-that-name >"$OUT" 2>&1
rc=$?

check_eq "a filter that matches no test file exits non-zero" 1 "$rc"
check_eq "the refusal names the pattern" 1 \
  "$(grep -c 'no-test-file-by-that-name' "$OUT")"
check_eq "the refusal names the glob it was matched against" 1 \
  "$(grep -c 'tests/x/test-\*\.sh' "$OUT")"
check_eq "and the run starts no test file" 0 \
  "$(grep -c '^seam tests on ' "$OUT")"
if [ "$rc" -eq 0 ]; then
  sed 's/^/    /' "$OUT"
fi

finish
