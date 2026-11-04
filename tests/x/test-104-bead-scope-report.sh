#!/usr/bin/env bash
# scripts/bead-scope.sh answers one question - is this bead's work in this
# worktree, or in the harness that a work/<bead> branch may not edit - and a
# wrong "yes, workable here" is the answer that costs a session, because the
# worker then reads the harness and finds a fix it cannot land. So the verdict
# is in scripts/lib/bead-scope-report.sh and this feeds it recorded beads and
# recorded file lists instead of needing a tracker or a harness checkout:
#
#   - a fix named in the description, and one named only in a comment;
#   - a bare file name, which is how most of these beads are written;
#   - a name the harness used to have and no longer does, which is the stale
#     description that sends a worker looking for a file that moved;
#   - a name both trees carry, and a name that is a suffix of one but not at a
#     path boundary, both of which must stay quiet;
#   - a brace list, which tokenises as the directory, and a wpy/ path when the
#     harness file list is empty: both still name the harness;
#   - the harness directory computed from a worktree and from the main checkout,
#     which are not the same number of levels up.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/bead-scope-report.sh
. "$ROOT_DIR/scripts/lib/bead-scope-report.sh"

DIR="$TMPDIR_SEAM/bead-scope"
rm -rf "$DIR"
mkdir -p "$DIR"

REPO="$DIR/repo-files.txt"
HARNESS="$DIR/harness-files.txt"
cat >"$REPO" <<'EOF'
README.md
docs/README.md
docs/QUALITY.md
scripts/bead-scope.sh
src/components/tiling.c
tests/x/test-104-bead-scope-report.sh
EOF
cat >"$HARNESS" <<'EOF'
HANDOFF.md
PROCESS.md
README.md
worker.py
wpy/commands/land.py
wpy/commands/publish.py
wpy/commands/review.py
tests/test_harness.py
EOF

bead() { # bead <id> <description>
  cat >"$DIR/bead.json" <<EOF
[{"id": "$1", "status": "in_progress", "title": "a bead", "description": "$2"}]
EOF
}

no_comments="$DIR/no-comments.json"
printf '[]\n' >"$no_comments"

LAST="$DIR/last.txt"

bead wm-b1 'The tiling path drops a client whose size hints are not a multiple of the gap.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a bead about this tree passes" 0 "$?"
check_eq "and says the work is here" 1 "$(grep -c 'nothing in its text names the harness, so its work is in this worktree' "$LAST")"
check_eq "and prints no finding" 0 "$(grep -c 'OUTSIDE' "$LAST")"

bead wm-b2 'The fix is one edit in wpy/commands/publish.py, where the reset runs.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a description naming a harness path fails" 3 "$?"
check_eq "and the path is named" 1 "$(grep -c 'OUTSIDE  wpy/commands/publish.py' "$LAST")"
check_eq "and the verdict says a work branch cannot carry it" 1 "$(grep -c 'no commit on a work/wm-b2 branch' "$LAST")"
check_eq "and it says to record the rest in a bd comment" 1 "$(grep -c 'record the' "$LAST")"

# A clean description with the harness named in the first comment, which is
# where the second occurrence of a bead's shape goes. The comments are part of
# what a bead says about itself, so they are read.
bead wm-b3 'Review the verdict before merging.'
cat >"$DIR/comments.json" <<'EOF'
[{"text": "Starting point is wpy/commands/review.py, which posts the marker twice."}]
EOF
bead_scope_verify "$DIR/bead.json" "$DIR/comments.json" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a path named only in a comment fails" 3 "$?"
check_eq "and the comment's path is named" 1 "$(grep -c 'OUTSIDE  wpy/commands/review.py' "$LAST")"

# A bare file name is how most of these beads are written, and the answer has to
# name the path a reader can open, not the fragment they wrote.
bead wm-b4 'publish.py:548 resets master whether or not it moved.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a bare file name fails" 3 "$?"
check_eq "and the finding is the full path" 1 "$(grep -c 'OUTSIDE  wpy/commands/publish.py' "$LAST")"

# The same file named twice is one finding: a verdict a reader has to scan twice
# for the same path reads as two problems.
bead wm-b5 'Both publish.py and wpy/commands/publish.py hold the reset.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "one file named two ways is reported once" 1 "$(grep -c 'OUTSIDE' "$LAST")"

# The stale description: it names a file the harness has since turned into a
# stub, so it resolves to nothing. A check that only reads the file list says
# "workable here" and sends the worker looking for the file.
bead wm-b6 'worker.sh merge needs the review-window guard.'
printf 'worker.py\nwpy/commands/land.py\n' >"$DIR/harness-moved.txt"
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$DIR/harness-moved.txt" >"$LAST" 2>&1
check_eq "a harness name the checkout no longer has fails" 3 "$?"
check_eq "and the finding says the harness left a stub there" 1 "$(grep -c 'the stub the harness left where it used to be started' "$LAST")"

# The harness checkout may be absent - a CI runner, a fresh clone - and the
# names it is known by are then the whole check.
: >"$DIR/harness-none.txt"
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$DIR/harness-none.txt" >"$LAST" 2>&1
check_eq "a named harness file fails without a harness checkout" 3 "$?"

# A name both trees carry is this repository's: both checkouts have a README.md
# and both have a docs/README.md, and a check that answered "outside" for them
# would refuse every bead that edits this repository's own documentation.
bead wm-b7 'README.md and docs/README.md#current-state are stale about the focus path.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a path both trees carry passes" 0 "$?"
check_eq "and prints no finding" 0 "$(grep -c 'OUTSIDE' "$LAST")"

# The match is at a path boundary, so a fragment that ends with a harness file's
# name and starts mid-path - a path copied from a wrapped line - is not that
# file. Without the boundary rule any suffix resolves, and a bead about this
# tree fails for a path it never named.
bead wm-b8 'a fragment like ds/publish.py is not a path, and src/components/tiling.c is ours.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a suffix that is not a path boundary passes" 0 "$?"

# A brace list does not survive tokenising as file names: the `{` splits
# wpy/commands/{publish,land}.py into the directory and the words. The directory
# is still the harness, and a check that only resolves whole files says the work
# is here.
bead wm-b9 'The reset is in wpy/commands/{publish,land}.py and nowhere else.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" >"$LAST" 2>&1
check_eq "a brace list fails" 3 "$?"
check_eq "and the directory that survived tokenising is named" 1 "$(grep -c 'OUTSIDE  wpy/commands$' "$LAST")"

# No harness checkout: the file list is empty, so a full wpy/ path cannot be
# resolved to a tracked file. It still names the package, and "workable here" is
# the answer the wrapper's header says is worse than no answer.
bead wm-b10 'The fix is one edit in wpy/commands/publish.py, where the reset runs.'
: >"$DIR/harness-empty.txt"
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$DIR/harness-empty.txt" >"$LAST" 2>&1
check_eq "a wpy path fails without a harness checkout" 3 "$?"
check_eq "and the path is still named" 1 "$(grep -c 'OUTSIDE  wpy/commands/publish.py' "$LAST")"

# Three levels up from a worktree under .worktrees/<bead> is beside the main
# checkout. The same three levels up from the main checkout is not, and dispatch
# runs this script from the main checkout before a worktree exists.
saved_harness=${WORKER_HARNESS-}
unset WORKER_HARNESS
common=$(git -C "$ROOT_DIR" rev-parse --path-format=absolute --git-common-dir)
main=$(dirname "$common")
expected="$(cd "$main/.." && pwd)/ai-workers-for-wm"
check_eq "a worktree resolves the harness beside the main checkout" "$expected" "$(bead_scope_harness_dir "$ROOT_DIR")"
check_eq "the main checkout resolves the same directory" "$expected" "$(bead_scope_harness_dir "$main")"
wrong=$(realpath -m "$main/../../../ai-workers-for-wm")
got=$(bead_scope_harness_dir "$main")
if [ -z "$got" ] || [ "$got" = "$wrong" ]; then
  fail "main checkout path is three levels up (${got:-empty})"
else
  pass "main checkout path is not three levels up"
fi
WORKER_HARNESS=/tmp/bead-scope-override
check_eq "WORKER_HARNESS overrides the computed path" "/tmp/bead-scope-override" "$(bead_scope_harness_dir "$ROOT_DIR")"
if [ -n "$saved_harness" ]; then
  WORKER_HARNESS=$saved_harness
else
  unset WORKER_HARNESS
fi

# A bead id that cannot be read has no answer, and the caller must not read that
# as "workable here": the wrapper exits 2 for it and 0 only for a clean bead.
# A missing harness checkout is a diagnostic, not a verdict line: it goes to
# stderr so a caller reading the report does not treat it as a finding.
if command -v bd >/dev/null 2>&1; then
  "$ROOT_DIR/scripts/bead-scope.sh" wm-no-such-bead >"$LAST" 2>&1
  check_eq "an unreadable bead is not an answer" 2 "$?"
  check_eq "and says it cannot read it" 1 "$(grep -c 'cannot read' "$LAST")"

  WORKER_HARNESS="$DIR/no-such-harness" "$ROOT_DIR/scripts/bead-scope.sh" wm-no-such-bead >"$LAST" 2>"$DIR/err.txt"
  check_eq "a missing harness is not part of the verdict" 0 "$(grep -c 'no harness checkout' "$LAST")"
  check_eq "a missing harness is reported on stderr" 1 "$(grep -c 'no harness checkout' "$DIR/err.txt")"
fi

finish
