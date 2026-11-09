#!/usr/bin/env bash
#
# check-bead-acceptance.sh - fail when criteria a baseline lists are gone, or a
# live bead has none at all.
#
# Usage: scripts/check-bead-acceptance.sh [--update-baseline] [-v|--verbose]
#
# `bd update <id> --acceptance ""` prints "Updated issue" and exits 0, and so
# does `--acceptance "$(...)"` when the substitution produced nothing. The
# description gate watches the description; this watches the other free-form
# field a rewrite goes through, and --acceptance has no stdin form for bd to
# refuse an empty body with, so the erasure is silent on the way in as well.
#
# bd omits the key when the field is empty, so the tracker output cannot tell a
# wipe from a bead that never had acceptance criteria, and the two need separate
# baselines:
#
#   tests/quality/bead-acceptance-baseline.txt
#     the ids that had text when the gate landed. One of those with empty text,
#     whitespace only, or missing from the list entirely fails the commit.
#   tests/quality/bead-acceptance-missing-baseline.txt
#     the live issues that had none when the second half landed. An issue
#     missing from here that carries no criteria fails the commit: a bead
#     written without them leaves the worker, and the review, nothing to check
#     done against, and nothing else in the tree looks at that field.
#
# --update-baseline writes both, from the same read of the tracker.
#
# The list comes from the tracker, so the check skips cleanly where there is no
# database (CI, a fresh clone), the same way scripts/check-bead-descriptions.sh
# does. When the tracker list carries no acceptance_criteria key at all, the
# JSONL export beside the database is read instead: that file is gitignored, so
# a worktree usually has only the tracker, and the export is the fallback for a
# bd whose list output stopped carrying the field.
#
# Exit status: 0 when every baselined issue still carries its acceptance text
# and every other live issue has criteria, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BASELINE="$ROOT_DIR/tests/quality/bead-acceptance-baseline.txt"
MISSING_BASELINE="$ROOT_DIR/tests/quality/bead-acceptance-missing-baseline.txt"
VERBOSE=0
UPDATE=0
for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    --update-baseline) UPDATE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *)
      echo "check-bead-acceptance: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1; }

if ! need bd || ! need jq; then
  echo "check-bead-acceptance: bd or jq missing, cannot read the tracker; skipping"
  exit 0
fi

ISSUES=$(mktemp "${TMPDIR:-/tmp}/bead-acceptance.XXXXXX")
EXPORT_JSON=$(mktemp "${TMPDIR:-/tmp}/bead-acceptance-export.XXXXXX")
trap 'rm -f "$ISSUES" "$EXPORT_JSON"' EXIT

if ! bd list --json --all >"$ISSUES" 2>/dev/null || [ ! -s "$ISSUES" ]; then
  echo "check-bead-acceptance: no beads database in this checkout; skipping"
  exit 0
fi

# A tracker that answers with something other than an issue list is not a
# tracker with no issues: skipping keeps a machine without the database from
# failing a commit it can say nothing about.
if ! jq -e 'type == "array"' "$ISSUES" >/dev/null 2>&1; then
  echo "check-bead-acceptance: the tracker did not answer with an issue list; skipping"
  exit 0
fi

# bd writes no key for an empty field, so a list where no issue has the key is
# either a tracker with no acceptance text at all or a bd whose list output
# stopped carrying it. The export beside the database carries the field, and
# reading it is what tells those two apart.
if ! jq -e 'any(.[]; has("acceptance_criteria"))' "$ISSUES" >/dev/null 2>&1; then
  EXPORT="$ROOT_DIR/.beads/issues.jsonl"
  if [ -s "$EXPORT" ] && jq -s 'length > 0' "$EXPORT" >/dev/null 2>&1; then
    if ! jq -s '.' "$EXPORT" >"$EXPORT_JSON"; then
      echo "check-bead-acceptance: $EXPORT is not a JSONL issue export, so nothing was read"
      exit 1
    fi
    ISSUES=$EXPORT_JSON
    echo "check-bead-acceptance: the tracker list carries no acceptance_criteria field; reading $EXPORT"
  fi
fi

# shellcheck source=lib/bead-acceptance-report.sh
. "$ROOT_DIR/scripts/lib/bead-acceptance-report.sh"

if [ "$UPDATE" -eq 1 ]; then
  bead_acceptance_write_baseline "$BASELINE" "$ISSUES"
  bead_acceptance_write_missing_baseline "$MISSING_BASELINE" "$ISSUES"
  echo "check-bead-acceptance: wrote $BASELINE and $MISSING_BASELINE from the tracker; read them before committing"
  exit 0
fi

# Both halves run whatever the first answers: a commit that wipes one bead's
# criteria and creates another without any is one reader paying for one commit,
# and the second FAIL line is what says so.
STATUS=0
BEAD_ACCEPTANCE_VERBOSE=$VERBOSE bead_acceptance_verify "$ISSUES" "$BASELINE" check-bead-acceptance || STATUS=1
BEAD_ACCEPTANCE_VERBOSE=$VERBOSE bead_acceptance_verify_missing "$ISSUES" "$MISSING_BASELINE" check-bead-acceptance || STATUS=1
exit "$STATUS"
