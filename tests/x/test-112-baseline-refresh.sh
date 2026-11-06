#!/usr/bin/env bash
#
# test-112-baseline-refresh.sh - a baseline refresh reproduces the committed file.
#
# Both reviewed baselines are written whole by their generator: the header comes
# from the generator, not from the committed file. A line somebody added to the
# file by hand is therefore gone at the next --update-baseline, and the diff
# mixes a real entry change with the header that was lost. The committed files
# do not carry such a line now; this regenerates each file from the entries it
# already holds and fails when the result is not byte for byte what is committed,
# so a hand-written line fails here instead of disappearing in a refresh.
#
# No X server is needed: the decisions are the merge in
# scripts/lib/seam-baseline.sh and the writer in scripts/lib/async-baseline.sh.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/seam-baseline.sh
. "$ROOT_DIR/scripts/lib/seam-baseline.sh"
# shellcheck source=../../scripts/lib/async-baseline.sh
. "$ROOT_DIR/scripts/lib/async-baseline.sh"

DIR="$TMPDIR_SEAM/baseline-refresh"
rm -rf "$DIR"
mkdir -p "$DIR"

SEAM_COMMITTED="$ROOT_DIR/tests/quality/seam-baseline.txt"
SEAM_GENERATED="$DIR/seam-baseline.txt"
SEAM_STARTED="$DIR/seam-started.txt"
SEAM_MEASURED="$DIR/seam-measured.txt"

# Every test file started, and every committed entry measured, is the refresh
# that has nothing to change. Anything the committed file carries that the
# generator does not write - a header line, a blank line, an ordering - shows up
# as a diff.
for f in "$ROOT_DIR"/tests/x/test-*.sh; do basename "$f"; done >"$SEAM_STARTED"
seam_baseline_entries "$SEAM_COMMITTED" >"$SEAM_MEASURED"
seam_baseline_install "$SEAM_GENERATED" "$ROOT_DIR/tests/x" "$SEAM_STARTED" "$SEAM_MEASURED"
seam_rc=$?
check_eq "a seam refresh succeeds" 0 "$seam_rc"
check_eq "a seam refresh reproduces tests/quality/seam-baseline.txt" "" \
  "$(diff -u "$SEAM_COMMITTED" "$SEAM_GENERATED")"

ASYNC_COMMITTED="$ROOT_DIR/tests/quality/async-baseline.txt"
ASYNC_GENERATED="$DIR/async-baseline.txt"
mapfile -t ASYNC_ENTRIES < <(awk '!/^#/ && NF' "$ASYNC_COMMITTED")
async_baseline_write "$ASYNC_GENERATED" "${ASYNC_ENTRIES[@]}"
async_rc=$?
check_eq "an async refresh succeeds" 0 "$async_rc"
check_eq "an async refresh reproduces tests/quality/async-baseline.txt" "" \
  "$(diff -u "$ASYNC_COMMITTED" "$ASYNC_GENERATED")"

check_eq "the async gate writes the baseline through async_baseline_write" 1 \
  "$(grep -c 'async_baseline_write ' "$ROOT_DIR/scripts/check-async.sh")"

finish
