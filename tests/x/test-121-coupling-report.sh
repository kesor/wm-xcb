#!/usr/bin/env bash
# scripts/check-coupling.sh exists because a component that includes another
# component's header calls it directly, bypassing the hub, and the compiler
# accepts it: the rule in docs/PHILOSOPHY.md section 2.1 was the reviewer's to
# keep. The gate reads git's index of the repository it sits in, so this copies
# it into a fixture repository and runs it there. The shapes that matter are:
#
#   - the including file's own header and a header of the same component, in
#     both forms the Makefile resolves: the path from the repository root and
#     the bare name;
#   - a header outside src/components/, and a system header, which are shared;
#   - another component's header written from the repository root, by its bare
#     name, with a ./ before that name, with a /./ in the path, and in angle
#     brackets;
#   - an angle-bracket bare name, which -I. does not resolve to a component
#     header;
#   - a name that differs from the including file's by a trailing s, which is
#     another component unless the pair is keybindings.h and keybinding.h;
#   - a search that did not run, which is not a clean tree;
#   - the tree as committed, which has to pass its own gate.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/coupling-gate"
rm -rf "$DIR"
mkdir -p "$DIR"

GATE="$ROOT_DIR/scripts/check-coupling.sh"
LAST="$DIR/last.txt"

# repo <name> - a fixture repository holding a copy of the gate, so the gate
# reads that repository's index instead of this tree's. Prints its path.
repo() {
  local r="$DIR/$1"
  rm -rf "$r"
  mkdir -p "$r/scripts"
  cp "$GATE" "$r/scripts/check-coupling.sh"
  git -C "$r" init -q
  printf '%s' "$r"
}

# tracked_at <repo> <path> - writes the lines on stdin into a tracked file.
tracked_at() {
  local r=$1 path=$2
  mkdir -p "$r/$(dirname "$path")"
  cat >"$r/$path"
  git -C "$r" add -A
}

R=$(repo shared)
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "tiling.h"
#include "src/components/tiling.h"
#include "src/core/wm-hub.h"
#include "src/sm/sm-names.h"
#include <stdint.h>
EOF
tracked_at "$R" src/components/keybinding.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/keybindings.h <<'EOF'
#include "keybinding.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "own headers, shared headers and a system header pass" 0 "$?"
check_eq "and nothing is printed" 0 "$(output_lines "$LAST")"
"$R/scripts/check-coupling.sh" --verbose >"$LAST" 2>&1
check_eq "the verbose pass line counts the files it searched" 1 \
  "$(grep -c 'check-coupling: no component includes another component.s header in 3 tracked file(s)' "$LAST")"

# One at a time, so a rule that only handles the first spelling fails here
# rather than on the branch of whoever includes the next one.
coupling_case() { # coupling_case <name> <path> <line>
  local r
  r=$(repo "$1")
  tracked_at "$r" "$2" <<EOF
$3
EOF
  "$r/scripts/check-coupling.sh" >"$LAST" 2>&1
  check_eq "$1 fails the gate" 1 "$?"
  check_eq "$1 is named by file and line" 1 "$(grep -c "^  OTHER COMPONENT  $2:1:" "$LAST")"
}
coupling_case root-path src/components/tiling.c '#include "src/components/floating.h"'
coupling_case angled src/components/tiling.c '#include <src/components/floating.h>'
coupling_case dot-in-root-path src/components/tiling.c '#include "src/./components/floating.h"'
coupling_case angled-dot-in-root-path src/components/tiling.c '#include <src/./components/floating.h>'
coupling_case leading-dot-root-path src/components/tiling.c '#include "./src/components/floating.h"'

# The bare name resolves from the including file's directory, so the header it
# names has to be there for the include to build at all: the fixture carries
# client-list.h, and tag-manager.c reaches it without the path.
R=$(repo bare-name)
tracked_at "$R" src/components/client-list.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/tag-manager.c <<'EOF'
#include "client-list.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "bare-name fails the gate" 1 "$?"
check_eq "bare-name is named by file and line" 1 \
  "$(grep -c '^  OTHER COMPONENT  src/components/tag-manager.c:1:' "$LAST")"

# A quoted bare name that no component file carries is somebody else's header,
# not another component's: the gate judges what a name resolves to, not the
# shape it is written in.
R=$(repo unrelated)
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "errors.h"
#include "src/core/wm-log.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "a quoted header outside src/components passes" 0 "$?"

# "./focus.h" is the header next to the including file. The compiler finds it;
# a gate that only matches a bare name or a src/components/ prefix does not.
R=$(repo dot-slash)
tracked_at "$R" src/components/focus.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "./focus.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "dot-slash fails the gate" 1 "$?"
check_eq "dot-slash is named by file and line" 1 \
  "$(grep -c '^  OTHER COMPONENT  src/components/tiling.c:1:' "$LAST")"
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "././focus.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "repeated dot-slash fails the gate" 1 "$?"

R=$(repo dot-own)
tracked_at "$R" src/components/tiling.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "./tiling.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "a component's own header written with ./ passes" 0 "$?"

# Angle brackets do not search the including file's directory, and -I. is the
# repository root, so <focus.h> is not src/components/focus.h. The gate that
# judges it by the bare name fails a line the compiler does not resolve.
R=$(repo angle-bare)
tracked_at "$R" src/components/focus.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/tiling.c <<'EOF'
#include <focus.h>
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "an angle-bracket bare name is not a component header" 0 "$?"

# keybindings.h including keybinding.h is the one same-component pair whose
# names differ. A trailing s on any other name is a different component.
R=$(repo trailing-s)
tracked_at "$R" src/components/focus.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/focuss.c <<'EOF'
#include "focus.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "a trailing s is another component" 1 "$?"
check_eq "the trailing-s include is named" 1 \
  "$(grep -c '^  OTHER COMPONENT  src/components/focuss.c:1:' "$LAST")"

R=$(repo counts)
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "src/components/floating.h"
EOF
tracked_at "$R" src/components/client-list.h <<'EOF'
#include "src/core/wm-hub.h"
EOF
tracked_at "$R" src/components/focus.c <<'EOF'
#include "client-list.h"
EOF
"$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "two findings exit 1" 1 "$?"
check_eq "and the summary counts both" 1 \
  "$(grep -c 'check-coupling: 2 include(s) name another component.s header' "$LAST")"

# A search that did not run is not a clean tree: git grep's 1 is "no match",
# and an empty result read as a pass is the one verdict a gate must never give.
R=$(repo broken)
tracked_at "$R" src/components/tiling.c <<'EOF'
#include "src/components/floating.h"
EOF
FAKE="$DIR/fake-bin"
rm -rf "$FAKE"
mkdir -p "$FAKE"
cat >"$FAKE/git" <<'EOF'
#!/bin/sh
echo "git: refused" >&2
exit 2
EOF
chmod +x "$FAKE/git"
PATH="$FAKE:$PATH" "$R/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "a search that did not run fails the gate" 1 "$?"
check_eq "and says the component files were not listed" 1 \
  "$(grep -c 'check-coupling: git ls-files failed, so the component files were not listed' "$LAST")"

# The tree this gate ships in has to pass it, and the hooks have to run it: a
# gate the commit hook skips leaves the rule where it was, and the merge and CI
# run the push list, not the commit one.
"$ROOT_DIR/scripts/check-coupling.sh" >"$LAST" 2>&1
check_eq "no component in this tree includes another's header" 0 "$?"
check_eq "the commit hook runs the coupling gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-commit | grep -cx '  coupling')"
check_eq "the push hook runs the coupling gate" 1 \
  "$("$ROOT_DIR/scripts/gate-list.sh" pre-push | grep -cx '  coupling')"
check_eq "QUALITY.md names the gate in its table" 1 \
  "$(grep -c "^| coupling | \`scripts/check-coupling.sh\` |" "$ROOT_DIR/docs/QUALITY.md")"

finish
