#!/usr/bin/env bash
#
# test-109-seam-baseline-filter.sh - a filtered run keeps the baseline entries of
# the tests it never started, and does not report them as stale.
#
# --update-baseline rewrote the file from the assertions the run saw fail, and a
# run with --filter sees the files whose name matched and nothing else: a
# filtered update deleted every entry for the rest. The run made it worse by
# calling those entries stale and naming --update-baseline as the command that
# drops them, so the report pointed at the destructive fix. scripts/check-
# complexity.sh took the merge route for the same defect, and this pins it for
# the seam suite.
#
# The decisions are in scripts/lib/seam-baseline.sh, so they are judged here on
# fixture files rather than by a whole seam run: what a run started, what it
# measured and what the baseline holds are exactly the inputs the library takes.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/seam-baseline.sh
. "$ROOT_DIR/scripts/lib/seam-baseline.sh"

DIR="$TMPDIR_SEAM/seam-baseline"
rm -rf "$DIR"
mkdir -p "$DIR"

BASELINE="$DIR/baseline.txt"
STARTED="$DIR/started.txt"
MEASURED="$DIR/measured.txt"
XTESTS="$ROOT_DIR/tests/x"

FOCUS="test-40-focus-on-manage.sh|the focus is on the managed client"
TAG="test-53-tag-move-client.sh|the moved client left the visible tag set"
EWMH="test-60-ewmh-root-properties.sh|_NET_CLIENT_LIST is set"
GONE="test-999-gone-with-its-file.sh|an assertion of a test that was deleted"

seed() {
  {
    echo "# Seam assertions that fail on a tree where the WM is not wired yet."
    printf '%s\n' "$FOCUS" "$TAG" "$EWMH" "$GONE"
  } >"$BASELINE"
}
merged() { seam_baseline_merge "$BASELINE" "$XTESTS" "$STARTED" <"$MEASURED"; }
stale() { seam_baseline_stale "$BASELINE" "$XTESTS" "$STARTED" "$MEASURED"; }

# A filtered run: only test-40 started, and its known failure is still there.
# The entries for the files it never started have to survive, or a run of one
# test erases what the suite knows about the others.
seed
printf '%s\n' "test-40-focus-on-manage.sh" >"$STARTED"
printf '%s\n' "$FOCUS" >"$MEASURED"
out=$(merged)
check_eq "a filtered update keeps the entries of the files it did not start" 2 \
  "$(printf '%s\n' "$out" | grep -cE '^test-(53|60)-')"
check_eq "and keeps the entry of the started file that still fails" 1 \
  "$(printf '%s\n' "$out" | grep -cF "$FOCUS")"
check_eq "and writes the started file's entry once, not twice" 3 \
  "$(printf '%s\n' "$out" | grep -c .)"
check_eq "and drops the entry of a test file that is gone" 0 \
  "$(printf '%s\n' "$out" | grep -cF "$GONE")"

# The same filtered run, after the started file's assertion was fixed: its entry
# goes, and only its entry.
: >"$MEASURED"
out=$(merged)
check_eq "a fixed assertion in a started file loses its entry" 0 \
  "$(printf '%s\n' "$out" | grep -cF "$FOCUS")"
check_eq "and the files the run did not start are still excused" 2 \
  "$(printf '%s\n' "$out" | grep -cE '^test-(53|60)-')"

# A new failure in the started file arrives with the entry it needs.
printf '%s\n' "test-40-focus-on-manage.sh|focus follows the pointer" >"$MEASURED"
out=$(merged)
check_eq "a new failure in a started file is added" 1 \
  "$(printf '%s\n' "$out" | grep -cF 'focus follows the pointer')"

# A full run is the case the rewrite was written for: every file started, so
# every entry that did not fail this time goes.
seed
printf '%s\n' "test-40-focus-on-manage.sh" "test-53-tag-move-client.sh" \
  "test-60-ewmh-root-properties.sh" >"$STARTED"
printf '%s\n' "$TAG" >"$MEASURED"
out=$(merged)
check_eq "a full update keeps only what failed" "$TAG" "$(printf '%s\n' "$out")"

# Stale, filtered: an entry for a file the run never started is not stale. It
# says nothing about this run, and the report used to name a fix that would
# delete it.
seed
printf '%s\n' "test-40-focus-on-manage.sh" >"$STARTED"
printf '%s\n' "$FOCUS" >"$MEASURED"
out=$(stale)
check_eq "an entry of a file the filtered run did not start is not stale" 0 \
  "$(printf '%s\n' "$out" | grep -cE '^test-(53|60)-')"
check_eq "and the only stale entry is the deleted test file's" "$GONE" "$out"

# Stale, filtered, with the started file's assertion now passing: its entry is
# stale, so the update that follows drops it.
: >"$MEASURED"
out=$(stale)
check_eq "an entry of a started file whose assertion passed is stale" 1 \
  "$(printf '%s\n' "$out" | grep -cF "$FOCUS")"
check_eq "and it is reported beside the deleted file's, not instead of it" 2 \
  "$(printf '%s\n' "$out" | grep -c .)"

# Stale, full run: every file started, so every entry that was not seen failing
# is stale - the report that keeps the list shrinking.
seed
printf '%s\n' "test-40-focus-on-manage.sh" "test-53-tag-move-client.sh" \
  "test-60-ewmh-root-properties.sh" >"$STARTED"
printf '%s\n' "$FOCUS" >"$MEASURED"
out=$(stale)
check_eq "a full run reports the entries whose assertions passed" 2 \
  "$(printf '%s\n' "$out" | grep -cE '^test-(53|60)-')"

# The replace itself. mv across filesystems unlinks the destination before the
# copy, so the temporary file has to be created beside the baseline. The path
# recorded here is the template mktemp was given.
seed
printf '%s\n' "test-40-focus-on-manage.sh" >"$STARTED"
printf '%s\n' "$FOCUS" >"$MEASURED"
# mktemp runs inside a command substitution, so a variable set there dies with
# the subshell. The path has to go to a file to be visible to the assertion.
MKTEMP_TEMPLATE_FILE="$DIR/mktemp-template"
rm -f "$MKTEMP_TEMPLATE_FILE"
mktemp() {
  printf '%s\n' "$1" >"$MKTEMP_TEMPLATE_FILE"
  command mktemp "$@"
}
seam_baseline_install "$BASELINE" "$XTESTS" "$STARTED" "$MEASURED"
install_rc=$?
unset -f mktemp
check_eq "installing the merged baseline succeeds" 0 "$install_rc"
check_eq "the temporary file is created beside the baseline" "$(dirname "$BASELINE")" \
  "$(dirname "$(cat "$MKTEMP_TEMPLATE_FILE")")"
check_eq "and the installed file keeps the entries of the files it did not start" 2 \
  "$(grep -cE '^test-(53|60)-' "$BASELINE")"
check_eq "and the installed file drops the entry of a test file that is gone" 0 \
  "$(grep -cF "$GONE" "$BASELINE")"
check_eq "and the installed file still carries the generated header" 1 \
  "$(grep -c '^# Generated by tests/x/run.sh --update-baseline\.$' "$BASELINE")"

check_eq "a filtered update does not say every other entry is unchanged" \
  "seam tests: --filter '106' started 1 test file(s); an entry whose test file exists and was not started is unchanged" \
  "$(seam_baseline_filter_note 106 1)"
check_eq "a run with no filter adds no such line" "" "$(seam_baseline_filter_note '' 12)"

check_eq "the runner replaces the baseline through seam_baseline_install" 1 \
  "$(grep -c 'seam_baseline_install ' "$ROOT_DIR/tests/x/run.sh")"
check_eq "and does not stage that file under the log directory" 0 \
  "$(grep -c 'LOG_DIR/seam-baseline' "$ROOT_DIR/tests/x/run.sh")"

finish
