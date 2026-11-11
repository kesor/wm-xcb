#!/usr/bin/env bash
#
# check-coupling.sh - fail when a component includes another component's header.
#
# Usage: scripts/check-coupling.sh [--verbose]
#
# docs/PHILOSOPHY.md section 2.1: a component never includes another
# component's header and never calls its functions. It sends a request through
# the hub and subscribes to events. The compiler accepts the include, so two
# components that call each other build and the rule is the reviewer's to keep.
# This is the device.
#
# A header is judged by the file that includes it. src/components/ is flat, so
# the header's name is the component's name. A quoted include is resolved two
# ways, and they are not the same mechanism: the including file's directory is
# searched first, which is what `#include "focus.h"` and `#include "./focus.h"`
# use, and the Makefile's -I. then searches the repository root, which is what
# `#include "src/components/focus.h"` uses. A `/./` in that path is the same
# header. Angle brackets skip the including file's directory, so `<focus.h>` is
# not a component header and `<src/components/focus.h>` is.
#
# Not a finding: the including file's own header, and a header of the same
# component. A component's files are named after it. The one exception in this
# tree is the pair keybindings.h and keybinding.h: keybindings.h is the
# keybinding component's user-editable table. A trailing s on any other name is
# a different component.
#
# Shared headers outside src/components/ are the answer, not a finding: the
# event names in src/event-types.h, the requests in src/core/wm-hub.h, and the
# names of a machine two components read in src/sm/sm-names.h.
#
# The line is judged as text, the way scripts/check-includes.sh judges an
# include: a comment that spells the shape is a finding too, and the fix is the
# same one of moving the name.
#
# Exit status: 0 when no file under src/components/ includes another
# component's header, 1 otherwise. `git grep`'s 1 is "no match", which is the
# answer this gate wants; anything above it is a search that did not run, and an
# empty result read as a clean tree is the one verdict a gate must never give.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

VERBOSE=0
case "${1:-}" in
  -v | --verbose) VERBOSE=1 ;;
  -h | --help)
    awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit 0
    ;;
  "") ;;
  *)
    echo "check-coupling: unknown argument '$1'" >&2
    exit 1
    ;;
esac

FILES=$(mktemp "${TMPDIR:-/tmp}/coupling-files.XXXXXX") || exit 1
HITS=$(mktemp "${TMPDIR:-/tmp}/coupling-hits.XXXXXX") || exit 1
FINDINGS=$(mktemp "${TMPDIR:-/tmp}/coupling-findings.XXXXXX") || exit 1
trap 'rm -f "$FILES" "$HITS" "$FINDINGS"' EXIT

# The tracked component files are the set a quoted bare include can name. A
# quoted bare name that is not in it is not a component header. An angle-bracket
# bare name is not one either: -I. puts the repository root on the search path,
# not src/components/, so <stdint.h> and <focus.h> are both outside this tree.
# <src/components/focus.h> is not: -I. finds that path.
if ! git ls-files 'src/components/*.c' 'src/components/*.h' >"$FILES"; then
  echo "check-coupling: git ls-files failed, so the component files were not listed" >&2
  exit 1
fi

# color.ui forced off so the finding it prints is a line a reader can grep for
# and paste. -a is required: without it a NUL in the file makes git grep print
# "Binary file <path> matches" and exit 0, and that line carries no include for
# the filter below to judge.
git -c color.ui=false grep -a -nE '#[[:space:]]*include[[:space:]]*[<"]' \
  -- 'src/components/*.c' 'src/components/*.h' >"$HITS" 2>/dev/null
case $? in
  0 | 1) ;;
  *)
    echo "check-coupling: git grep failed, so no component file was searched" >&2
    exit 1
    ;;
esac

# The include target is read out of the line, then judged by name: a header
# reached through src/components/ or a bare name of a tracked component file is
# a component header, and anything else is shared. Both stems lose their
# extension, so `client-list.h` and `client-list.c` compare as one name.
awk '
  FNR == NR {
    sub(/^.*\//, "", $0)
    known[$0] = 1
    next
  }
  {
    hit = $0
    file = hit
    sub(/:.*/, "", file)

    if (!match(hit, /include[[:space:]]*[<"][^">]+[">]/))
      next
    target = substr(hit, RSTART, RLENGTH)
    angled = (target ~ /</)
    sub(/^include[[:space:]]*[<"]/, "", target)
    sub(/[">]$/, "", target)

    # "./focus.h" is the header next to the including file, and
    # "src/./components/focus.h" is the root-path form. Collapse those before
    # judging, or both checks miss a header the compiler finds.
    while (sub(/^\.\//, "", target)) {}
    while (gsub(/\/\.\//, "/", target)) {}

    # Angle brackets do not search the directory of the including file. -I. is
    # the repository root, so <focus.h> is not a component header.
    if (angled && target !~ /^src\/components\//)
      next

    depth = split(target, part, "/")
    base  = part[depth]
    if (depth > 1) {
      if (target !~ /^src\/components\//)
        next
    } else if (!(target in known)) {
      next
    }

    sub(/\.[ch]$/, "", base)
    own = file
    sub(/^.*\//, "", own)
    sub(/\.[ch]$/, "", own)

    if (base == own)
      next
    # keybindings.h is the table for the keybinding component. The names differ
    # by the plural s, and that pair is the only one this accepts.
    if ((own == "keybindings" && base == "keybinding") ||
        (own == "keybinding" && base == "keybindings"))
      next

    print hit
  }
' "$FILES" "$HITS" >"$FINDINGS"

FOUND=$(wc -l <"$FINDINGS" | tr -d ' ')
if [ "${FOUND:-0}" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] &&
    echo "check-coupling: no component includes another component's header in $(wc -l <"$FILES" | tr -d ' ') tracked file(s)"
  exit 0
fi

sed 's/^/  OTHER COMPONENT  /' "$FINDINGS"
echo
echo "check-coupling: $FOUND include(s) name another component's header."
echo "Send the request through the hub, subscribe to the event, or move a name both sides need into a shared header under src/."
exit 1
