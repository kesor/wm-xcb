#!/usr/bin/env bash
# scripts/check-root.sh exists because the root filled with the hub, the XCB
# connection, the logger and the configuration over time, and nothing failed
# when the next one landed there. The allowlist is the verdict, so this feeds it
# recorded lists; the shapes that matter are:
#
#   - a root entry the allowlist names, whether a file or a directory;
#   - a source file at the root, which belongs under src/;
#   - any other file the allowlist does not name;
#   - an empty list, which is a failed git ls-files and not a tidy root.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/root-report.sh
. "$ROOT_DIR/scripts/lib/root-report.sh"

DIR="$TMPDIR_SEAM/root-report"
rm -rf "$DIR"
mkdir -p "$DIR"

ENTRIES="$DIR/entries.txt"
LAST="$DIR/last.txt"

cat >"$ENTRIES" <<'EOF'
AGENTS.md
Makefile
src
tests
docs
EOF
ROOT_VERBOSE=1 root_verify "$ENTRIES" check-root >"$LAST" 2>&1
check_eq "the files and directories on the allowlist pass" 0 "$?"
check_eq "and the pass line counts every entry" 1 \
  "$(grep -c 'check-root: 5 root entry(s), every one on the allowlist' "$LAST")"

printf 'wm-foo.c\n' >"$ENTRIES"
ROOT_VERBOSE=0 root_verify "$ENTRIES" check-root >"$LAST" 2>&1
check_eq "a source file at the root fails" 1 "$?"
check_eq "and is named" 1 "$(grep -c 'NOT ALLOWED  wm-foo.c' "$LAST")"
check_eq "and says where it belongs" 1 \
  "$(grep -c 'belongs in a directory under src/' "$LAST")"

printf 'notes.txt\n' >"$ENTRIES"
ROOT_VERBOSE=0 root_verify "$ENTRIES" check-root >"$LAST" 2>&1
check_eq "a file the allowlist does not name fails" 1 "$?"
check_eq "and is named" 1 "$(grep -c 'NOT ALLOWED  notes.txt' "$LAST")"
check_eq "and no source advice is printed for it" 0 \
  "$(grep -c 'belongs in a directory under src/' "$LAST")"

: >"$ENTRIES"
ROOT_VERBOSE=0 root_verify "$ENTRIES" check-root >"$LAST" 2>&1
check_eq "an empty list fails" 1 "$?"
check_eq "and says git gave nothing to check" 1 \
  "$(grep -c 'git ls-files gave nothing to check' "$LAST")"

# The script the hook runs reads git's index, so a tracked file at the root is
# what it sees. The tree this gate ships in must pass its own check.
"$ROOT_DIR/scripts/check-root.sh" >"$LAST" 2>&1
check_eq "this tree's root is on the allowlist" 0 "$?"

# The merge and the CI gates job run lefthook's pre-push list, not pre-commit.
# A gate named only on the commit hook is skipped by both, so a file committed
# with the hooks off lands at the root and nothing in CI names it.
check_eq "the push hook runs the root gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-push | grep -cx '  root')"
check_eq "the commit hook runs the root gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-commit | grep -cx '  root')"

finish
