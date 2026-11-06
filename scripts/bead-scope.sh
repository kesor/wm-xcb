#!/usr/bin/env bash
#
# bead-scope.sh - is this bead's work in this worktree, or in the harness?
#
# Usage: scripts/bead-scope.sh [<bead-id>...]
#
# With no id it reads the one out of the current branch, `work/<bead-id>`.
#
# A bead whose text names a file under `ai-workers-for-wm/` asks for work that
# no commit on a `work/<bead>` branch can carry: that checkout is the harness,
# a worker reads it and does not edit it. Finding that out by reading the
# harness costs a session its turns - the worker spent its whole run on it and
# the reviewer then spent another proving the branch was empty - so this answers
# the question in one command and names the paths it found.
#
# The verdict lives in scripts/lib/bead-scope-report.sh, which
# tests/x/test-104-bead-scope-report.sh feeds recorded beads and recorded file
# lists, so the shapes that matter are pinned without a tracker or a harness
# checkout.
#
# This is not a gate: which bead a branch is for is not in any committed file,
# and a check that cannot be wrong for a reason nobody can reproduce is not a
# gate. It is the answer to a question, and the caller decides.
#
# A second question is asked on the way, because it costs a caller the same
# session when it goes unanswered: a bead names files, and a bead written months
# ago can name one this tree deleted. Each path the text names is checked against
# the tracked files and against the history, and one that is gone is printed with
# the commit that removed it. The answer to that is "cannot tell" - the same text
# is a bead asking for a new file - so it is exit 2, the same as a read that
# failed, and never the "workable here" that would send a worker to resurrect
# what the tree removed on purpose.
#
# Exit status: 0 no token names the harness and every file-shaped token whose
# first component this tree tracks, or once tracked, is still here. 3 the text
# names a harness path, 2 the question could not be answered: a broken read, or
# a path this tree does not have. A bead can name a harness file
# and still ask for a change here; the verdict says to land that part. A broken
# read is 2 and not 0: an answer that says "workable here" because the tracker
# could not be reached is worse than no answer, and a caller that only tests for
# success would take it.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 2

for arg in "$@"; do
  case "$arg" in
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1; }

if ! need bd || ! need jq; then
  echo "bead-scope: bd or jq is missing, so the bead's text cannot be read" >&2
  exit 2
fi

# shellcheck source=lib/bead-scope-report.sh
. "$ROOT_DIR/scripts/lib/bead-scope-report.sh"

# Beside the main checkout, not three levels up from this script. A worktree
# and the main checkout are different depths, and dispatch runs this from the
# main checkout before a worktree exists.
HARNESS_DIR=$(bead_scope_harness_dir "$ROOT_DIR")

IDS=("$@")
if [ "${#IDS[@]}" -eq 0 ]; then
  branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
  case "$branch" in
    work/*) IDS=("${branch#work/}") ;;
    *)
      echo "bead-scope: no bead id given and this branch is '${branch:-unknown}', not work/<bead-id>" >&2
      echo "usage: scripts/bead-scope.sh <bead-id>..." >&2
      exit 2
      ;;
  esac
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/bead-scope.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

git -C "$ROOT_DIR" ls-files >"$WORK/repo.txt" 2>/dev/null || : >"$WORK/repo.txt"
# Every path a commit on any branch deleted, with the newest commit that removed
# it, so a bead that names one gets the commit and not a git walk.
bead_scope_deletion_log "$ROOT_DIR" | bead_scope_deletions >"$WORK/deleted.txt"
if ! git -C "$HARNESS_DIR" ls-files >"$WORK/harness.txt" 2>/dev/null; then
  : >"$WORK/harness.txt"
  echo "bead-scope: no harness checkout at $HARNESS_DIR, so only the names it is" >&2
  echo "            known by are checked, not the files it tracks" >&2
fi

STATUS=0
for id in "${IDS[@]}"; do
  if ! bd show "$id" --json >"$WORK/bead.json" 2>"$WORK/err.txt" || [ ! -s "$WORK/bead.json" ]; then
    echo "bead-scope: cannot read '$id' from the tracker: $(head -1 "$WORK/err.txt")" >&2
    STATUS=2
    continue
  fi
  if ! bd comments "$id" --json >"$WORK/comments.json" 2>/dev/null; then
    printf '[]\n' >"$WORK/comments.json"
  fi

  bead_scope_verify "$WORK/bead.json" "$WORK/comments.json" \
    "$WORK/repo.txt" "$WORK/harness.txt" "$WORK/deleted.txt"
  case $? in
    0) ;;
    3) [ "$STATUS" -eq 0 ] && STATUS=3 ;;
    *) STATUS=2 ;;
  esac
done

exit "$STATUS"
