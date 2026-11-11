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
# the header's name is the component's name, and the include may be written two
# ways: from the repository root (`#include "src/components/focus.h"`) or by its
# bare name (`#include "focus.h"`), which the Makefile's -I. resolves from the
# including file's directory. Both name another component's header, and a
# search for one form misses the other.
#
# Not a finding: the including file's own header, and a header of the same
# component. A component's files are named after it, with one exception in this
# tree: keybindings.h is the keybinding component's user-editable table, and it
# includes keybinding.h. A plural s is what separates them, so a name that
# differs from the including file's by one trailing s is the same component.
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

# The tracked component files are the set a bare include can name. A quoted
# bare name that is not in it is not a component header, and neither is an
# angle-bracket one: -I. puts the repository root on the system search path, not
# src/components/, so `<stdint.h>` and `<focus.h>` are both system headers.
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
    sub(/^include[[:space:]]*[<"]/, "", target)
    sub(/[">]$/, "", target)

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

    if (base == own || base == own "s" || own == base "s")
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
