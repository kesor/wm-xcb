#!/usr/bin/env bash
#
# check-includes.sh - fail when an include is not written from the repository root.
#
# Usage: scripts/check-includes.sh [--verbose]
#
# The Makefile compiles with -I., so `#include "src/core/wm-hub.h"` resolves
# from the repository root and names the same header in every file. Two shapes
# leave that rule:
#
#   `#include "../sm/sm.h"` walks up a directory. It resolves from the directory
#   the including file sits in, so it compiles wherever that file is today, and
#   when the file moves the compiler names the moved file, because the include
#   line it did not touch is somewhere else.
#
#   `#include "tiling.h"` beside tiling.c names a header with no directory at
#   all. It resolves by the same mechanism, from the including file's directory,
#   and breaks on the same move. The angle-bracket form is left alone: that is
#   how a system header is written, `#include <stdio.h>` names no repository
#   path, and there is no root-relative spelling for it to be missing.
#
#   vendor/ is not read for that second shape. Those headers are third-party
#   and are compiled through their own include root
#   (`-Ivendor/libxcb-errors/include`, included as `<xcb/errors.h>`). A sibling
#   quoted name resolves there. The root-relative spelling the failure line
#   prints resolves only through `-I.` and fails that compile. A walk up is
#   still a finding under vendor/: `../` leaves that include root the same way.
#
# Nothing failed when the next one landed, so this does.
#
# The line is judged as text, the way scripts/check-paths.sh judges a path: a
# comment that spells the shape is a finding too, and the fix is the same one
# character cheaper than explaining why the comment needs it.
#
# Exit status: 0 when every quoted include in the tracked .c and .h files
# outside vendor/ names a header by its path in the tree and no include walks
# up a directory, 1 otherwise. `git grep`'s 1 is "no match", which is the
# answer this gate wants; anything above it is a search that did not run, and
# an empty result read as a clean tree is the one verdict a gate must never
# give.

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

# The walk-up shape, anywhere in the line: a header path that walks up in the
# middle leaves the tree the same way one that starts with `../`, as in
# `#include "src/sm/../../core/wm-hub.h"`. The quote or the angle bracket is
# what tells an include line from prose about one.
WALKS_UP='#[[:space:]]*include[[:space:]]*[<"][^">]*\.\./'

# The bare-name shape: a quoted include whose name is a header file and nothing
# else. `[^"/]*` reads up to the closing quote, so a `../` after that quote is
# not this line's include path and a root-relative path is not a bare name.
BARE_NAME='#[[:space:]]*include[[:space:]]*"[^"/]*"'

HITS=$(mktemp "${TMPDIR:-/tmp}/include-hits.XXXXXX") || exit 1
trap 'rm -f "$HITS"' EXIT

# color.ui forced off so the finding it prints is a line a reader can grep for
# and paste, not one wrapped in paint codes. -a is required: without it a NUL in
# the file makes git grep print "Binary file <path> matches" and exit 0. That
# line does not contain either pattern, so the re-check below counts nothing and
# the gate exits 0 over the include it found.
#
# vendor/ is excluded from the bare-name search only. The reason is the header
# comment above: a sibling quoted name resolves through that directory's own
# -I, and a walk up still has to fail there.
search_includes() {
  local status
  git -c color.ui=false grep -a -nE -e "$1" -- "${@:2}" >>"$HITS" 2>/dev/null
  status=$?
  case "$status" in
    0 | 1) return 0 ;;
    *)
      echo "check-includes: git grep failed, so no tracked file was searched" >&2
      exit 1
      ;;
  esac
}

: >"$HITS"
search_includes "$WALKS_UP" '*.c' '*.h'
search_includes "$BARE_NAME" '*.c' '*.h' ':!vendor'

FOUND=0
# shape <pattern> <label> - prints the hits that match one shape, under the
# label that names it, and adds them to the count. One shape at a time so a
# pattern that only reads the first fails here instead of at somebody's move.
shape() {
  local pattern=$1 label=$2 count
  count=$(grep -cE -- "$pattern" "$HITS" || true)
  [ "${count:-0}" -eq 0 ] && return 0
  grep -E -- "$pattern" "$HITS" | sed "s/^/  $label  /"
  FOUND=$((FOUND + count))
}

shape "$WALKS_UP" "WALKS UP"
shape "$BARE_NAME" "NO DIRECTORY"

if [ "$FOUND" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] &&
    echo "check-includes: every include outside vendor/ in $(git ls-files '*.c' '*.h' ':!vendor' | wc -l) tracked source file(s) is written from the repository root"
  exit 0
fi

echo
echo "check-includes: $FOUND include(s) are not written from the repository root."
echo "Write the include by its path in the tree: #include \"src/core/wm-hub.h\""
exit 1
