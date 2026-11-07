#!/usr/bin/env bash
# scripts/check-paths.sh exists because this repository is public: a committed
# path that names somebody's home is wrong for every other checkout and every CI
# runner. The gate then failed on a fixture instead. A unit test built a fake
# home under /tmp, the pattern /home/[A-Za-z0-9._-]+/ matched the home directory
# inside that path, and the author renamed the fixture to get the commit
# through: the cheap way out was to weaken the test.
#
# The verdict is in scripts/lib/path-report.sh so this feeds it recorded git grep
# hits. The two answers that have to stay different, whichever way the pattern
# is rewritten:
#
#   - a directory a test builds for itself, which is not a home;
#   - a home directory or a personal source tree, which starts a field.
#
# The paths below are written out because this file is one of the files the
# gate searches past: spelling a home path is what it is here to do.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/path-report.sh
. "$ROOT_DIR/scripts/lib/path-report.sh"

DIR="$TMPDIR_SEAM/path-report"
rm -rf "$DIR"
mkdir -p "$DIR"

HITS="$DIR/hits.txt"
LAST="$DIR/last.txt"

# What the gate saw when it refused the commit: a fixture's own home directory,
# and the other shapes that are nobody's home either.
cat >"$HITS" <<'EOF'
tests/unit/test-config-file.c:417:  (void) setenv("HOME", "/tmp/home", 1);
tests/unit/test-config-file.c:419:  assert(strcmp(buffer, "/tmp/home/.config/wm/config") == 0);
tests/unit/test-config-file.c:420:  (void) setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
src/core/wm-config.c:88:  const char* relative = "src/home/foo/bar";
src/core/wm-config.c:89:  snprintf(buffer, sizeof buffer, "%s/config/wm", home);
EOF
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "a path a test builds for itself is not a home" 0 "$?"
check_eq "and nothing is printed for it" 0 "$(grep -c 'HOME PATH' "$LAST")"

# Nobody's home, and a home path: one line each, so a pattern that flags the
# fixture and lets the real path through fails here rather than at somebody's
# commit.
HOME_HITS="$DIR/home-hits.txt"
cat >"$HOME_HITS" <<'EOF'
docs/NOTES.md:3:  the reference checkout lives at /home/someone/src/wm
docs/NOTES.md:4:  the code is under /Users/someone/code
docs/NOTES.md:5:  the build ran in C:\Users\someone\code
docs/NOTES.md:6:  copy the tree to ~/src/wm first
docs/NOTES.md:7:  or to ~/suckless/wm
EOF
path_verify "$HOME_HITS" check-paths >"$LAST" 2>&1
check_eq "a home path fails the gate" 1 "$?"
check_eq "every one of the five is named" 5 "$(grep -c 'HOME PATH' "$LAST")"
check_eq "and the count is in the summary" 1 "$(grep -c 'check-paths: 5 line(s) name a home directory' "$LAST")"
check_eq "and the fix says what to write instead" 1 \
  "$(grep -c 'Use a path relative to the repository, or a placeholder like <path-to-repo>' "$LAST")"

# A home path is a finding wherever its field starts, at the beginning of the
# line or after a quote, and one inside a longer segment is not a home at all.
printf 'a.c:1: setenv("HOME", "/home/someone/wm", 1);\n' >"$HITS"
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "a home path in a string literal is a finding" 1 "$?"
check_eq "and the field boundary is not the end of the line" 1 "$(grep -c 'HOME PATH' "$LAST")"

printf 'a.c:1: setenv("HOME", "x/home/someone/wm", 1);\n' >"$HITS"
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "a home directory inside another path segment is not a home" 0 "$?"

printf 'a.c:1: #include "~/config/wm.h"\n' >"$HITS"
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "a home location the header says is not flagged stays unflagged" 0 "$?"

# The files that spell the pattern and the paths it looks for are the three the
# gate has to search past, and they are committed like everything else. The
# same three lines under a file that is not one of them are findings, so a skip
# the exemption did not make is told apart from a pattern that let them through.
cat >"$HITS" <<'EOF'
scripts/check-paths.sh:18:# /tmp/home/.config is a fixture and /home/someone/.config is a finding.
scripts/lib/path-report.sh:26:# a fixture under /tmp/home read as /home/someone/.config
tests/x/test-117-path-report.sh:30:docs/NOTES.md:3: /home/someone/src/wm
EOF
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "the gate does not flag a file for spelling the pattern" 0 "$?"
check_eq "and names nothing" 0 "$(grep -c 'HOME PATH' "$LAST")"

cat >"$HITS" <<'EOF'
scripts/check-paths.sh:18:# /tmp/home/.config is a fixture and /home/someone/.config is a finding.
scripts/lib/path-report.sh:26:# a fixture under /tmp/home read as /home/someone/.config
src/core/wm-config.c:30:docs/NOTES.md:3: /home/someone/src/wm
EOF
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "the same lines in a file that is not exempt are findings" 1 "$?"
check_eq "and the one that is not exempt is the one named" 1 "$(grep -c 'HOME PATH' "$LAST")"

# A search that found nothing is a pass: `git grep` exits 1, and the caller
# hands the gate the empty file that leaves behind.
: >"$HITS"
path_verify "$HITS" check-paths >"$LAST" 2>&1
check_eq "no hits is a clean tree" 0 "$?"

# The script searches tracked files in the worktree, from the repository root.
# The fixture it refused is back in the unit suite, so this assertion fails if
# the boundary is dropped again.
"$ROOT_DIR/scripts/check-paths.sh" >"$LAST" 2>&1
check_eq "this tree names no home directory" 0 "$?"
"$ROOT_DIR/scripts/check-paths.sh" --verbose >"$LAST" 2>&1
check_eq "and the verbose pass line counts the files it searched" 1 \
  "$(grep -cE 'check-paths: no home directory paths in [0-9]+ tracked files' "$LAST")"

# git grep's own defaults disagree with the re-check. A binary match is the line
# "Binary file <path> matches", which does not contain the pattern, so a verdict
# that only re-applies the pattern calls a hit a clean tree. A color.ui of
# always paints the file:line: prefix even when stdout is a file, and the
# exemption then misses the files it searches past. Both are pinned on the
# search the gate runs, not on a recorded line.
REPO="$DIR/binary-repo"
rm -rf "$REPO"
mkdir -p "$REPO"
git -C "$REPO" init -q
printf 'note /home/someone/src\0more' >"$REPO/notes.dat"
git -C "$REPO" add notes.dat
(
  cd "$REPO" || exit 1
  path_hits "$DIR/binary-hits.txt"
)
path_verify "$DIR/binary-hits.txt" check-paths >"$LAST" 2>&1
check_eq "a home path in a binary file is a finding" 1 "$?"

REPO="$DIR/col0-repo"
rm -rf "$REPO"
mkdir -p "$REPO"
git -C "$REPO" init -q
printf '/home/someone/src\n' >"$REPO/col0.txt"
printf '/tmp/home/.config/wm/config\n' >"$REPO/fixture.txt"
git -C "$REPO" add col0.txt fixture.txt
(
  cd "$REPO" || exit 1
  path_hits "$DIR/col0-hits.txt"
)
check_eq "a home path at column 0 is a hit" 1 \
  "$(grep -c '^col0.txt:1:/home/someone/src$' "$DIR/col0-hits.txt")"
check_eq "and a fixture under /tmp is not a git grep hit" 0 \
  "$(grep -c 'fixture.txt' "$DIR/col0-hits.txt")"

GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=color.ui GIT_CONFIG_VALUE_0=always \
  "$ROOT_DIR/scripts/check-paths.sh" >"$LAST" 2>&1
check_eq "forced color does not flag the files the gate searches past" 0 "$?"

# Exit 1 is "no match". Anything above it is a search that did not run, and an
# empty file read as a clean tree is the verdict the gate must not give.
FAKE="$DIR/fake-bin"
mkdir -p "$FAKE"
cat >"$FAKE/git" <<'EOF'
#!/bin/sh
echo "git: refused" >&2
exit 2
EOF
chmod +x "$FAKE/git"
PATH="$FAKE:$PATH" "$ROOT_DIR/scripts/check-paths.sh" >"$LAST" 2>&1
check_eq "a search that did not run fails the gate" 1 "$?"
check_eq "and the refusal says no file was searched" 1 \
  "$(grep -c 'check-paths: git grep failed, so no tracked file was searched' "$LAST")"

finish
