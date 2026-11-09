#!/usr/bin/env bash
#
# check-bead-descriptions.sh - fail when a bead's description is gone.
#
# Usage: scripts/check-bead-descriptions.sh [--update-baseline] [-v|--verbose]
#
# `bd update <id> --description ""` prints "Updated issue" and exits 0, and so
# does `--description "$(...)"` when the substitution produced nothing, so a
# description is the one part of the tracker that can be erased with a success
# message. Nothing watches that field, and the erase is silent until someone
# reads the bead. This does watch it: an issue with no description, or with one
# made of whitespace, fails the commit and is named along with where the text
# before the wipe is kept.
#
# The rewrite path is `bd update <id> --stdin`, which refuses an empty body;
# AGENTS.md says so, and this gate is what catches the shapes that get past it.
#
# The list comes from the tracker, so the check skips cleanly where there is no
# database (CI, a fresh clone), the same way scripts/check-bead-refs.sh does.
# tests/quality/bead-description-baseline.txt holds the issues whose empty
# description the gate accepts. It is empty: every issue had one when the gate
# landed, so an empty description from now on is one that was lost or never
# written. --update-baseline only drops the entries that are no longer needed;
# it refuses to record a new one, because recording it is how the gate was
# turned green over a wipe.
#
# Exit status: 0 when every issue carries a description, 1 otherwise, including
# when --update-baseline refuses to record an empty one.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BASELINE="$ROOT_DIR/tests/quality/bead-description-baseline.txt"
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
      echo "check-bead-descriptions: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1; }

if ! need bd || ! need jq; then
  echo "check-bead-descriptions: bd or jq missing, cannot read the tracker; skipping"
  exit 0
fi

ISSUES=$(mktemp "${TMPDIR:-/tmp}/bead-descriptions.XXXXXX")
trap 'rm -f "$ISSUES"' EXIT

if ! bd list --json --all >"$ISSUES" 2>/dev/null || [ ! -s "$ISSUES" ]; then
  echo "check-bead-descriptions: no beads database in this checkout; skipping"
  exit 0
fi

# A tracker that answers with something other than an issue list is not a
# tracker with no issues: skipping keeps a machine without the database from
# failing a commit it can say nothing about.
if ! jq -e 'type == "array"' "$ISSUES" >/dev/null 2>&1; then
  echo "check-bead-descriptions: the tracker did not answer with an issue list; skipping"
  exit 0
fi

# shellcheck source=lib/bead-description-report.sh
. "$ROOT_DIR/scripts/lib/bead-description-report.sh"

if [ "$UPDATE" -eq 1 ]; then
  if ! bead_description_write_baseline "$BASELINE" "$ISSUES" check-bead-descriptions; then
    exit 1
  fi
  echo "check-bead-descriptions: wrote $BASELINE from the tracker; read it before committing"
  exit 0
fi

BEAD_DESCRIPTION_VERBOSE=$VERBOSE
bead_description_verify "$ISSUES" "$BASELINE" check-bead-descriptions
