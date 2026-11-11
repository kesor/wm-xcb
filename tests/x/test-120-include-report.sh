#!/usr/bin/env bash
# scripts/check-includes.sh exists because an include that walks up a directory
# compiles wherever the including file sits: it is invisible until a move breaks
# it, and then the compiler names the moved file rather than the include line
# nobody touched. The gate reads git's index of the repository it sits in, so
# this copies it into a fixture repository and runs it there. The shapes that
# matter are:
#
#   - an include written from the repository root, and the angle-bracket form,
#     which are not a walk up;
#   - a quoted include that walks up, an angle-bracket one, one indented inside
#     an #if, and one that walks up in the middle of its path;
#   - a comment that spells the shape, which the gate judges as text;
#   - a NUL in the file, which makes git grep call it binary and, without -a,
#     print "Binary file <path> matches" instead of the include line;
#   - a ../ after the closing quote, which is not the included header's path;
#   - a search that did not run, which is not a clean tree;
#   - the tree as committed, which has to pass its own gate.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/include-gate"
rm -rf "$DIR"
mkdir -p "$DIR"

GATE="$ROOT_DIR/scripts/check-includes.sh"
LAST="$DIR/last.txt"

# repo <name> - a fixture repository holding a copy of the gate, so the gate
# reads that repository's index instead of this tree's. Prints its path.
repo() {
  local r="$DIR/$1"
  rm -rf "$r"
  mkdir -p "$r/scripts"
  cp "$GATE" "$r/scripts/check-includes.sh"
  git -C "$r" init -q
  printf '%s' "$r"
}

# source_at <repo> <path> <line> - writes one line into a tracked .c file, so
# the next run of the gate has something to search.
source_at() {
  local r=$1 path=$2 line=$3
  mkdir -p "$r/$(dirname "$path")"
  printf '%s\n' "$line" >"$r/$path"
  git -C "$r" add -A
}

R=$(repo clean)
source_at "$R" src/core/wm-hub.c '#include "src/core/wm-log.h"'
source_at "$R" src/target/client.h '#include <xcb/xcb.h>'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "the root form and an angle-bracket header pass" 0 "$?"
check_eq "and nothing is printed" 0 "$(output_lines "$LAST")"
"$R/scripts/check-includes.sh" --verbose >"$LAST" 2>&1
check_eq "the verbose pass line counts the files it searched" 1 \
  "$(grep -c 'check-includes: no include walks up a directory in 2 tracked source file(s)' "$LAST")"

# One at a time, so a pattern that only handles the first shape fails here
# rather than at somebody's move.
walk_case() { # walk_case <name> <path> <line>
  local r
  r=$(repo "$1")
  source_at "$r" "$2" "$3"
  "$r/scripts/check-includes.sh" >"$LAST" 2>&1
  check_eq "$1 fails the gate" 1 "$?"
  check_eq "$1 is named by file and line" 1 "$(grep -c "^  RELATIVE INCLUDE  $2:1:" "$LAST")"
}
walk_case quoted src/components/tiling.c '#include "../core/wm-hub.h"'
walk_case angled src/components/tiling.c '#include <../core/wm-hub.h>'
walk_case indented src/components/tiling.c '  #include "../sm/sm.h"'
walk_case mid-path src/core/wm-hub.c '#include "src/sm/../../core/wm-log.h"'
walk_case commented src/core/wm-hub.c '/* never #include "../core/wm-log.h" */'

# The include is on its own line. The NUL is what makes git call the file binary,
# so the gate has to ask for the line (-a) rather than the binary summary.
R=$(repo binary)
mkdir -p "$R/src"
printf '#include "../core/wm-hub.h"\n\0\n' >"$R/src/t.c"
git -C "$R" add -A
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a walking include in a file git calls binary fails the gate" 1 "$?"
check_eq "and the file is named, not reported as a binary match" 1 \
  "$(grep -c '^  RELATIVE INCLUDE  src/t.c:1:' "$LAST")"

R=$(repo counts)
source_at "$R" src/core/wm-hub.c '#include "../core/wm-log.h"'
source_at "$R" src/target/client.c '#include "../sm/sm.h"'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "two findings exit 1" 1 "$?"
check_eq "and the summary counts both" 1 \
  "$(grep -c 'check-includes: 2 include(s) walk up a directory' "$LAST")"

# The ../ is after the closing quote, so it is not the included header's path:
# the gate has to read the shape, not search the line for the two characters.
R=$(repo trailing)
source_at "$R" src/core/wm-hub.c '#include "src/core/wm-log.h" /* ../ is not this line'"'"'s path */'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a ../ after the closing quote passes" 0 "$?"

# A fixture with no source at all is a pass: git grep's 1 is "no match", and
# the gate searches *.c and *.h only.
R=$(repo empty)
printf 'notes\n' >"$R/README"
git -C "$R" add -A
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a repository with no C source passes" 0 "$?"

# Exit 1 is "no match". Anything above it is a search that did not run, and an
# empty result read as a clean tree is the verdict the gate must not give.
R=$(repo broken)
source_at "$R" src/core/wm-hub.c '#include "../core/wm-log.h"'
FAKE="$DIR/fake-bin"
mkdir -p "$FAKE"
cat >"$FAKE/git" <<'EOF'
#!/bin/sh
echo "git: refused" >&2
exit 2
EOF
chmod +x "$FAKE/git"
PATH="$FAKE:$PATH" "$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a search that did not run fails the gate" 1 "$?"
check_eq "and says no file was searched" 1 \
  "$(grep -c 'check-includes: git grep failed, so no tracked file was searched' "$LAST")"

# The tree this gate ships in has to pass it, and the hooks have to run it: a
# gate the commit hook skips leaves the rule where it was, and the merge and CI
# run the push list, not the commit one.
"$ROOT_DIR/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "this tree's includes are written from the root" 0 "$?"
check_eq "the commit hook runs the include gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-commit | grep -cx '  includes')"
check_eq "the push hook runs the include gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-push | grep -cx '  includes')"
check_eq "QUALITY.md names the gate in its table" 1 \
  "$(grep -c "^| includes | \`scripts/check-includes.sh\` |" "$ROOT_DIR/docs/QUALITY.md")"

finish
