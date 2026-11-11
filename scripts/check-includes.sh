#!/usr/bin/env bash
#
# check-includes.sh - fail when an include walks up a directory.
#
# Usage: scripts/check-includes.sh [--verbose]
#
# The Makefile compiles with -I., so `#include "src/core/wm-hub.h"` resolves
# from the repository root and names the same header in every file. An include
# that walks up resolves from the directory the including file sits in: it
# compiles wherever that file is today, and when the file moves the compiler
# names the moved file, because the include line it did not touch is somewhere
# else. Nothing failed when the next one landed, so this does.
#
# The line is judged as text, the way scripts/check-paths.sh judges a path: a
# comment that spells the shape is a finding too, and the fix is the same one
# character cheaper than explaining why the comment needs it.
#
# Exit status: 0 when no tracked .c or .h includes a header by walking up a
# directory, 1 otherwise. `git grep`'s 1 is "no match", which is the answer this
# gate wants; anything above it is a search that did not run, and an empty
# result read as a clean tree is the one verdict a gate must never give.

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
    echo "check-includes: unknown argument '$1'" >&2
    exit 1
    ;;
esac

# The shape, anywhere in the line: a header path that walks up in the middle
# leaves the tree the same way one that starts with `../`, as in
# `#include "src/sm/../../core/wm-hub.h"`. The quote or the angle bracket is
# what tells an include line from prose about one.
PATTERN='#[[:space:]]*include[[:space:]]*[<"][^">]*\.\./'

HITS=$(mktemp "${TMPDIR:-/tmp}/include-hits.XXXXXX") || exit 1
trap 'rm -f "$HITS"' EXIT

# color.ui forced off so the finding it prints is a line a reader can grep for
# and paste, not one wrapped in paint codes.
git -c color.ui=false grep -nE "$PATTERN" -- '*.c' '*.h' >"$HITS" 2>/dev/null
case $? in
  0 | 1) ;;
  *)
    echo "check-includes: git grep failed, so no tracked file was searched" >&2
    exit 1
    ;;
esac

FOUND=$(grep -cE "$PATTERN" "$HITS" || true)
if [ "${FOUND:-0}" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] &&
    echo "check-includes: no include walks up a directory in $(git ls-files '*.c' '*.h' | wc -l) tracked source file(s)"
  exit 0
fi

sed 's/^/  RELATIVE INCLUDE  /' "$HITS"
echo
echo "check-includes: $FOUND include(s) walk up a directory."
echo "Write the include from the repository root: #include \"src/core/wm-hub.h\"."
exit 1
