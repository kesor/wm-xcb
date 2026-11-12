#!/usr/bin/env bash
# scripts/check-includes.sh exists because an include that is not written from
# the repository root compiles only while the including file stays where it is:
# it walks up a directory, or it names a header with no directory at all, and
# both resolve from the including file's own directory. It is invisible until a
# move breaks it, and then the compiler names the moved file rather than the
# include line nobody touched. The gate reads git's index of the repository it
# sits in, so this copies it into a fixture repository and runs it there. The
# shapes that matter are:
#
#   - an include written from the repository root, an angle-bracket system
#     header, which has no directory and is not a repository path, and the
#     angle-bracket form of a project header;
#   - a quoted include that walks up, an angle-bracket one, one indented, and
#     one that walks up in the middle of its path;
#   - a quoted include whose name is a bare header file, which is the other half
#     of the same rule, alone and indented;
#   - a bare name under vendor/, which is not a finding, and a walk up there,
#     which still is: those headers are compiled through their own include root;
#   - a comment that spells either shape, which the gate judges as text;
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
source_at "$R" src/core/wm-log.c '#include <stdio.h>'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "the root form and an angle-bracket header pass" 0 "$?"
check_eq "and nothing is printed" 0 "$(output_lines "$LAST")"
"$R/scripts/check-includes.sh" --verbose >"$LAST" 2>&1
check_eq "the verbose pass line counts the files it searched" 1 \
  "$(grep -c 'check-includes: every include outside vendor/ in 3 tracked source file(s) is written from the repository root' "$LAST")"

# One at a time, so a pattern that only handles the first shape fails here
# rather than at somebody's move. The label in the output names which shape it
# was, so a bare name read as a walk up is a finding a reader can argue with.
bad_case() { # bad_case <name> <label> <path> <line>
  local r
  r=$(repo "$1")
  source_at "$r" "$3" "$4"
  "$r/scripts/check-includes.sh" >"$LAST" 2>&1
  check_eq "$1 fails the gate" 1 "$?"
  check_eq "$1 is named by file and line" 1 "$(grep -c "^  $2  $3:1:" "$LAST")"
}
bad_case quoted WALKS\ UP src/components/tiling.c '#include "../core/wm-hub.h"'
bad_case angled WALKS\ UP src/components/tiling.c '#include <../core/wm-hub.h>'
bad_case indented WALKS\ UP src/components/tiling.c '  #include "../sm/sm.h"'
bad_case mid-path WALKS\ UP src/core/wm-hub.c '#include "src/sm/../../core/wm-log.h"'
bad_case commented WALKS\ UP src/core/wm-hub.c '/* never #include "../core/wm-log.h" */'
bad_case bare NO\ DIRECTORY src/components/tiling.c '#include "tiling.h"'
bad_case bare-indented NO\ DIRECTORY src/components/tiling.c '  #include "tiling.h"'
bad_case bare-commented NO\ DIRECTORY src/core/wm-hub.c '/* never #include "wm-hub.h" */'

# vendor/ is third-party and is included through its own -I root. A sibling
# quoted name resolves there; the root-relative spelling the gate prints does
# not. A walk up still leaves that root, so it is still a finding. One case
# each, so an exclusion that drops vendor/ from both searches fails here.
R=$(repo vendor-bare)
source_at "$R" vendor/libxcb-errors/include/xcb/errors.h '#include "xcb_errors.h"'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a bare include under vendor/ passes" 0 "$?"
check_eq "and nothing is printed for it" 0 "$(output_lines "$LAST")"
R=$(repo vendor-walk)
source_at "$R" vendor/libxcb-errors/include/xcb/errors.h '#include "../xcb_errors.h"'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a walk up under vendor/ still fails" 1 "$?"
check_eq "and it is named as a walk up" 1 \
  "$(grep -c '^  WALKS UP  vendor/libxcb-errors/include/xcb/errors.h:1:' "$LAST")"

# The include is on its own line. The NUL is what makes git call the file binary,
# so the gate has to ask for the line (-a) rather than the binary summary.
R=$(repo binary)
mkdir -p "$R/src"
printf '#include "../core/wm-hub.h"\n\0\n' >"$R/src/t.c"
git -C "$R" add -A
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "a walking include in a file git calls binary fails the gate" 1 "$?"
check_eq "and the file is named, not reported as a binary match" 1 \
  "$(grep -c '^  WALKS UP  src/t.c:1:' "$LAST")"

# One finding of each shape, so the count is a sum over both patterns and a
# gate that stopped after the first shape reports one where there are two.
R=$(repo counts)
source_at "$R" src/core/wm-hub.c '#include "../core/wm-log.h"'
source_at "$R" src/target/client.c '#include "client.h"'
"$R/scripts/check-includes.sh" >"$LAST" 2>&1
check_eq "two findings exit 1" 1 "$?"
check_eq "and the summary counts both" 1 \
  "$(grep -c 'check-includes: 2 include(s) are not written from the repository root' "$LAST")"

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

# The header -Ivendor/libxcb-errors/include finds as <xcb/errors.h>. -I. is not
# on this command: a root-relative quoted include compiles only with that flag
# and fails here, which is why the bare-name check does not read vendor/.
read -r -a xcb_cflags <<<"$(pkg-config --cflags xcb)"
gcc "${xcb_cflags[@]}" -I"$ROOT_DIR/vendor/libxcb-errors/include" -fsyntax-only -xc - >"$LAST" 2>&1 <<'EOF'
#include <xcb/errors.h>
EOF
check_eq "vendored errors.h resolves through its own include root" 0 "$?"
check_eq "the commit hook runs the include gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-commit | grep -cx '  includes')"
check_eq "the push hook runs the include gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-push | grep -cx '  includes')"
check_eq "QUALITY.md names the gate in its table" 1 \
  "$(grep -c "^| includes | \`scripts/check-includes.sh\` |" "$ROOT_DIR/docs/QUALITY.md")"

finish
