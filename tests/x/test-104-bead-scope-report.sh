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
#   - the harness named without a path, which is a bead whose fix is one of its
#     commands: `cmd_start`, and the word "harness", resolve to no file, and the
#     verdict used to read "its work is in this worktree";
#   - a path this tree deleted, which is a bead written before the file went and
#     looks exactly like one asking for a change to a file that is here: the
#     path, the commit that removed it, and the directory that is still tracked;
#   - a path under a top-level directory the tree removed entirely, which
#     ls-files cannot root and the deletion list can;
#   - a path no commit ever tracked, which is a bead asking for a new file as
#     far as the text can tell, and answers the same "cannot tell" without
#     sending the caller to look for a replacement; it may also be a file
#     another bead delivered under another name, so the verdict says to grep for
#     the behaviour before writing it;
#   - a branch name, a ref, and the fragments a glob leaves, none of which is a
#     path in this tree and none of which may turn the answer into "cannot
#     tell";
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
scripts/check-async.sh
scripts/lib/async-scan.sh
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

# The second list the verdict reads: every path a commit deleted, with the
# newest commit that removed it. The two agent-script paths are the real shape
# this caught - commit 61b23 deleted them along with the GitHub-era workflow.
DELETED="$DIR/deleted.txt"
cat >"$DELETED" <<'EOF'
.cursor/rules/beads.mdc	b2435
docs/guides/standing-on-the-shoulders.html	52d68
hooks/format.sh	9aa01
scripts/review-agent.sh	61b23
scripts/task-template.md	61b23
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
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a bead about this tree passes" 0 "$?"
check_eq "and says the work is here" 1 "$(grep -c 'so its work is in this worktree' "$LAST")"
check_eq "and prints no finding" 0 "$(grep -c 'OUTSIDE\|GONE' "$LAST")"

bead wm-b2 'The fix is one edit in wpy/commands/publish.py, where the reset runs.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
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
bead_scope_verify "$DIR/bead.json" "$DIR/comments.json" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a path named only in a comment fails" 3 "$?"
check_eq "and the comment's path is named" 1 "$(grep -c 'OUTSIDE  wpy/commands/review.py' "$LAST")"

# A bare file name is how most of these beads are written, and the answer has to
# name the path a reader can open, not the fragment they wrote.
bead wm-b4 'publish.py:548 resets master whether or not it moved.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a bare file name fails" 3 "$?"
check_eq "and the finding is the full path" 1 "$(grep -c 'OUTSIDE  wpy/commands/publish.py' "$LAST")"

# The same file named twice is one finding: a verdict a reader has to scan twice
# for the same path reads as two problems.
bead wm-b5 'Both publish.py and wpy/commands/publish.py hold the reset.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "one file named two ways is reported once" 1 "$(grep -c 'OUTSIDE' "$LAST")"

# The stale description: it names a file the harness has since turned into a
# stub, so it resolves to nothing. A check that only reads the file list says
# "workable here" and sends the worker looking for the file.
bead wm-b6 'worker.sh merge needs the review-window guard.'
printf 'worker.py\nwpy/commands/land.py\n' >"$DIR/harness-moved.txt"
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$DIR/harness-moved.txt" "$DELETED" >"$LAST" 2>&1
check_eq "a harness name the checkout no longer has fails" 3 "$?"
check_eq "and the finding says the harness left a stub there" 1 "$(grep -c 'the stub the harness left where it used to be started' "$LAST")"

# The harness checkout may be absent - a CI runner, a fresh clone - and the
# names it is known by are then the whole check.
: >"$DIR/harness-none.txt"
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$DIR/harness-none.txt" "$DELETED" >"$LAST" 2>&1
check_eq "a named harness file fails without a harness checkout" 3 "$?"

# A name both trees carry is this repository's: both checkouts have a README.md
# and both have a docs/README.md, and a check that answered "outside" for them
# would refuse every bead that edits this repository's own documentation.
bead wm-b7 'README.md and docs/README.md#current-state are stale about the focus path.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a path both trees carry passes" 0 "$?"
check_eq "and prints no finding" 0 "$(grep -c 'OUTSIDE' "$LAST")"

# The match is at a path boundary, so a fragment that ends with a harness file's
# name and starts mid-path - a path copied from a wrapped line - is not that
# file. Without the boundary rule any suffix resolves, and a bead about this
# tree fails for a path it never named.
bead wm-b8 'a fragment like ds/publish.py is not a path, and src/components/tiling.c is ours.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a suffix that is not a path boundary passes" 0 "$?"

# A brace list does not survive tokenising as file names: the `{` splits
# wpy/commands/{publish,land}.py into the directory and the words. The directory
# is still the harness, and a check that only resolves whole files says the work
# is here.
bead wm-b9 'The reset is in wpy/commands/{publish,land}.py and nowhere else.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a brace list fails" 3 "$?"
check_eq "and the directory that survived tokenising is named" 1 "$(grep -c 'OUTSIDE  wpy/commands$' "$LAST")"

# No harness checkout: the file list is empty, so a full wpy/ path cannot be
# resolved to a tracked file. It still names the package, and "workable here" is
# the answer the wrapper's header says is worse than no answer.
bead wm-b10 'The fix is one edit in wpy/commands/publish.py, where the reset runs.'
: >"$DIR/harness-empty.txt"
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$DIR/harness-empty.txt" "$DELETED" >"$LAST" 2>&1
check_eq "a wpy path fails without a harness checkout" 3 "$?"
check_eq "and the path is still named" 1 "$(grep -c 'OUTSIDE  wpy/commands/publish.py' "$LAST")"

# The harness named without a path. The text says `cmd_start` and "the harness"
# and never a file, and the token scan finds nothing: the verdict read "its work
# is in this worktree", and the worker read the harness to find out. This is a
# recording of that text, not the text: the comment that names the command also
# names a live tracker id, and a script that contains one fails
# scripts/check-bead-refs.sh. The paths it names are in the repo fixture above
# so they are not GONE findings; the word check is what answers. The harness
# file list is empty, which is the checkout the tool was tried against.
cat >"$DIR/bead.json" <<'EOF'
[{"id": "wm-b21", "status": "in_progress", "title": "the worktree is reset while the agent is already reading it",
  "description": "What I tried: read the files wm-b21 names in .worktrees/wm-b21 (git log showed HEAD e37b7, work/wm-b21 at that commit)\nWhat happened: scripts/check-async.sh and scripts/lib/async-scan.sh had changed content, and the draft I had written into scripts/lib/async-scan.sh was gone.\nWhat worked: re-reading every file the issue names and redoing the draft against the new tree.\nSuggested fix: make worktree prep finish before the agent's first tool call, and log the reset with the target sha."}]
EOF
cat >"$DIR/comments.json" <<'EOF'
[{"text": "I could not find the command that ran the reset in the harness: `cmd_start` refuses an existing worktree, and the only `git reset --hard` in the harness is publish's on the merge ref."}]
EOF
bead_scope_verify "$DIR/bead.json" "$DIR/comments.json" "$REPO" "$DIR/harness-empty.txt" "$DELETED" >"$LAST" 2>&1
check_eq "the harness named by word and command is not an answer" 2 "$?"
check_eq "and the words that named it are printed" 1 "$(grep -c 'talks about the harness: cmd_start and harness' "$LAST")"
check_eq "and it does not answer that the work is here" 0 "$(grep -c 'so its work is in this worktree' "$LAST")"
check_eq "and it says it cannot tell" 1 "$(grep -c 'cannot say whether the work is here' "$LAST")"
check_eq "and it sends the reader to the bead, not to the harness" 1 "$(grep -c 'Read the bead for the file the fix is in' "$LAST")"
check_eq "and it does not say the fix is a harness command" 0 \
  "$(grep -c 'A fix in the harness is a command' "$LAST")"
check_eq "and a command with no file here is not this tree's work" 1 \
  "$(grep -c 'A cmd_ name with no file here is not this tree'"'"'s work' "$LAST")"

# Either half names the harness on its own: a bead that says "the harness" and
# no command, and one that names a command and never the word. Both name the
# checkout and no file in it.
bead wm-b22 'Read the harness for where the round limit is enforced.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "the word alone is not an answer" 2 "$?"
check_eq "and the word is named" 1 "$(grep -c 'talks about the harness: harness\.$' "$LAST")"

bead wm-b23 "cmd_review's exit status is ignored, so an approval reads as a pass."
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a harness command alone is not an answer" 2 "$?"
check_eq "and the command is named" 1 "$(grep -c 'talks about the harness: cmd_review\.$' "$LAST")"

# "worktree" is not one of the words. Every bead here says "in this worktree",
# so a rule that fired on it would answer "cannot tell" for most of the queue.
bead wm-b24 'Land it in this worktree once the reset is logged.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "the word worktree alone is still an answer" 0 "$?"
check_eq "and it says the work is here" 1 "$(grep -c 'so its work is in this worktree' "$LAST")"

# The other half of the question. A bead is written months before it is picked
# up, and one that asks for changes to a file commit 61b23 deleted reads the
# same as one that asks for changes to a file that is here. "Workable here" is
# what sent a worker to resurrect the workflow that commit removed.
bead wm-b11 'scripts/review-agent.sh runs the review, and its checks belong next to it.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a path this tree deleted is not an answer" 2 "$?"
check_eq "and the path is named" 1 "$(grep -c 'GONE     scripts/review-agent.sh' "$LAST")"
check_eq "and the commit that deleted it is named" 1 "$(grep -c 'deleted by 61b23' "$LAST")"
check_eq "and the directory that is still tracked is named" 1 "$(grep -c 'scripts/ is still tracked, so look there for what replaced it' "$LAST")"
check_eq "and a deleted path gets no advice about a sibling bead" 0 "$(grep -c 'another bead may have delivered it under' "$LAST")"
check_eq "and it does not answer that the work is here" 0 "$(grep -c 'so its work is in this worktree' "$LAST")"

# Two tokens, one path: the full path and the bare name are one finding, for the
# same reason the harness list reports one.
bead wm-b12 'review-agent.sh is scripts/review-agent.sh, named both ways.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "one deleted file named two ways is reported once" 1 "$(grep -c 'GONE' "$LAST")"

# A bare name the history carries: the tokeniser keeps a file name with no
# directory, and a check that ignored it would miss half the beads.
bead wm-b13 'format.sh stopped checking the hooks.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a bare name the history carries is reported" 2 "$?"
check_eq "and with the path a reader can open" 1 "$(grep -c 'GONE     hooks/format.sh' "$LAST")"

# A path whose directory is gone too says so: the caller's next step is a
# different one when there is nothing to read where the file used to sit.
bead wm-b14 'The guide at docs/guides/standing-on-the-shoulders.html is where this belongs.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a deleted path whose directory is gone answers the same" 2 "$?"
check_eq "and says nothing holds that directory now" 1 "$(grep -c 'nothing under docs/guides/ is tracked now' "$LAST")"

# A bead asking for a file that is not here yet reads identically, and the
# answer is the same "cannot tell": saying "not here" would refuse most of the
# queue, and saying "here" is the wrong answer for the deleted file.
bead wm-b15 'Add scripts/check-widget.sh, which fails when a widget has no owner.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a file no commit ever tracked is not an answer either" 2 "$?"
check_eq "and the path is named" 1 "$(grep -c 'GONE     scripts/check-widget.sh' "$LAST")"
check_eq "and it says a new file reads the same" 1 "$(grep -c 'may be asking for a new file' "$LAST")"
check_eq "and says another bead may have delivered the behaviour under another name" 1 "$(grep -c 'another bead may have delivered it under' "$LAST")"
check_eq "and says the bead's text is what needs correcting" 1 "$(grep -c "bead's text is what needs correcting" "$LAST")"
check_eq "and the verdict says a stale name reads as a missing file" 1 "$(grep -c "makes the bead's text stale and the file not missing" "$LAST")"
check_eq "and the tracked directory is named" 1 "$(grep -c 'scripts/ is still tracked$' "$LAST")"
check_eq "and it does not send the caller looking for a replacement" 0 "$(grep -c 'look there for what replaced it' "$LAST")"

# ls-files has no entry under a directory the tree deleted entirely, which is
# also true of work/ and origin/. A root check that only reads the tracked list
# drops the token and answers "work is here" for a path a commit removed.
bead wm-b19 'The rule lives in .cursor/rules/beads.mdc.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a path under a removed top-level directory is not an answer" 2 "$?"
check_eq "and the path is named" 1 "$(grep -c 'GONE     .cursor/rules/beads.mdc' "$LAST")"
check_eq "and the commit that deleted it is named" 1 "$(grep -c 'deleted by b2435' "$LAST")"
check_eq "and it does not say that directory is still tracked" 0 "$(grep -c 'still tracked' "$LAST")"
check_eq "and it says nothing under that directory is tracked" 1 "$(grep -c 'nothing under .cursor/rules/ is tracked now' "$LAST")"
check_eq "and it does not answer that the work is here" 0 "$(grep -c 'so its work is in this worktree' "$LAST")"

# A file-shaped token whose first component this tree never tracked and never
# deleted is still not a path here. Dropping the root check to catch .cursor/
# would report this and make "cannot tell" the answer for most of the queue.
bead wm-b20 'not-a-tree/left-pad.sh is not a file in this tree.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a file under a root this tree never had is not a path here" 0 "$?"
check_eq "and it is not reported" 0 "$(grep -c 'GONE\|OUTSIDE' "$LAST")"

# What is not a path in this tree: a branch, a ref, a name whose first component
# is not tracked here, and the fragments a glob leaves behind. Reporting any of
# them would make "cannot tell" the answer for most of the queue, and a verdict
# that fires on everything says nothing.
bead wm-b16 'Land it on work/wm-b16 once refs/kept/wm-b16 and origin/master agree, then add tests/x/test-*.sh.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a branch, a ref and a glob are not paths here" 0 "$?"
check_eq "and none of them is reported" 0 "$(grep -c 'GONE\|OUTSIDE' "$LAST")"

# A directory is in the tree when the entries under it are, and a bead writes it
# with the trailing slash the tokeniser strips.
bead wm-b17 'The tiling code lives in src/components/ and nowhere else.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a tracked directory is not a lost path" 0 "$?"

# A bead that names both: the harness verdict stands, because that part is
# certain, and the lost path is printed next to it rather than instead of it.
bead wm-b18 'The reset in wpy/commands/publish.py replaced scripts/review-agent.sh.'
bead_scope_verify "$DIR/bead.json" "$no_comments" "$REPO" "$HARNESS" "$DELETED" >"$LAST" 2>&1
check_eq "a bead naming the harness and a lost path is still the harness verdict" 3 "$?"
check_eq "and the harness path is printed" 1 "$(grep -c 'OUTSIDE  wpy/commands/publish.py' "$LAST")"
check_eq "and the lost path is printed next to it" 1 "$(grep -c 'GONE     scripts/review-agent.sh' "$LAST")"
check_eq "and the lost path is called out before the worker plans" 1 "$(grep -c 'It also names a path this tree does not have' "$LAST")"

# The parser reads what git prints: a commit line, its paths, and a blank line
# between records. A path deleted twice is one finding, and the commit a reader
# wants is the newest, because that is the one that removed what they seek.
cat >"$DIR/deletion-log.txt" <<'EOF'
commit 52d68
docs/guides/standing-on-the-shoulders.html

commit e98a4
docs/guides/standing-on-the-shoulders.html

commit 61b23
scripts/review-agent.sh
scripts/task-template.md

commit 4c0de

EOF
found=$(bead_scope_deletions <"$DIR/deletion-log.txt")
commit_of() { printf '%s\n' "$found" | awk -F'\t' -v p="$1" '$1 == p { print $2 }'; }
check_eq "the newest commit that deleted a path is the one reported" "52d68" \
  "$(commit_of docs/guides/standing-on-the-shoulders.html)"
check_eq "a path one commit deleted keeps that commit" "61b23" "$(commit_of scripts/review-agent.sh)"
check_eq "a commit that deleted nothing adds no path" 3 "$(printf '%s\n' "$found" | grep -c .)"

# The walk the wrapper runs, end to end in a throwaway history: the parser is
# only worth its lines when git answers it with a deletion, and this is the
# shape (--all, --no-renames, quotePath off) that has to keep answering.
HIST="$DIR/history-repo"
rm -rf "$HIST"
mkdir -p "$HIST/scripts"
git -C "$HIST" init -q -b main
printf 'one\n' >"$HIST/scripts/deleted-later.sh"
printf 'two\n' >"$HIST/keep.sh"
git -C "$HIST" add -A
git -C "$HIST" -c user.name=fixture -c user.email=fixture@example.invalid \
  -c commit.gpgsign=false commit -q -m "add both"
rm "$HIST/scripts/deleted-later.sh"
git -C "$HIST" add -A
git -C "$HIST" -c user.name=fixture -c user.email=fixture@example.invalid \
  -c commit.gpgsign=false commit -q -m "delete one"
walk=$(bead_scope_deletion_log "$HIST" | bead_scope_deletions)
check_eq "the walk names the path a commit deleted" 1 \
  "$(printf '%s\n' "$walk" | awk -F'\t' '$1 == "scripts/deleted-later.sh" { n++ } END { print n + 0 }')"
check_eq "and names a commit for it" 1 \
  "$(printf '%s\n' "$walk" | awk -F'\t' '$1 == "scripts/deleted-later.sh" && $2 ~ /^[0-9a-f]+$/ { n++ } END { print n + 0 }')"
check_eq "and leaves a file nobody deleted alone" 0 "$(printf '%s\n' "$walk" | grep -c '^keep.sh')"

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

# The printed verdict says to grep before writing a never-tracked path. A worker
# reads two other texts first: the wrapper's --help, and the procedure in the
# bootstrap skill. Either one still saying the path is only a new file sends
# the worker to write it, which is the reading the verdict exists to stop. The
# report's own header is the third copy, and a reader who trusts it restores
# the old reading.
help=$("$ROOT_DIR/scripts/bead-scope.sh" --help)
check_eq "the help text says to grep for the behaviour first" 1 \
  "$(printf '%s\n' "$help" | grep -c 'grep for the behaviour')"
check_eq "the help text does not say a missing path is only a new file" 0 \
  "$(printf '%s\n' "$help" | grep -c 'is a bead asking for a new file')"
check_eq "the help text names the bead that talks about the harness" 1 \
  "$(printf '%s\n' "$help" | grep -c 'without naming a file in it')"
check_eq "and the help names the answer for the word alone" 1 \
  "$(printf '%s\n' "$help" | grep -c 'a bead that talks about the harness')"

proc="$ROOT_DIR/.agents/skills/wm-bootstrap/SKILL.md"
check_eq "the worker procedure says to grep for the behaviour first" 1 \
  "$(grep -c 'grep for the behaviour' "$proc")"
check_eq "the worker procedure does not say a missing path is only a new file" 0 \
  "$(grep -c 'asking for a new file as far as the text can tell' "$proc")"
check_eq "the worker procedure does not say the fix is a harness command" 0 \
  "$(grep -c 'the fix is a command there' "$proc")"
check_eq "the worker procedure says the word also matches this tree" 1 \
  "$(grep -c 'own seam harness' "$proc")"

lib="$ROOT_DIR/scripts/lib/bead-scope-report.sh"
check_eq "the report's own description does not say a missing path is only a new file" 0 \
  "$(grep -c 'a new file as far as the text can tell' "$lib")"
check_eq "and it does not stop at the caller looking" 0 \
  "$(grep -c 'asking for a new file looks the same from the text, and the caller looks' "$lib")"

finish
