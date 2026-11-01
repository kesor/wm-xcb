#!/usr/bin/env bash
#
# check-doc-refs.sh - fail when a document points at a line number.
#
# Usage: scripts/check-doc-refs.sh [--verbose]
#
# A reference like `wm-xcb.c:212` is wrong the moment anyone adds a line above
# it, so every code change turns into prose edits in files that have nothing to
# do with the change. Documentation names symbols: functions, types, constants,
# targets, beads ids. The living record of a defect is its beads issue, which
# may cite lines because it is a snapshot with a date on it.
#
# Not scanned: README.md, which is a legacy concatenation scheduled for
# replacement (bd show wm-utz.7).
#
# Exit status: 0 when no document cites a line number, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
FILES=()
for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) FILES+=("$arg") ;;
  esac
done

# file.c:123, file.h:12-34, Makefile:90, quality.yml:42
PATTERN='\b[A-Za-z0-9_./-]+\.(c|h|yml|yaml|nix|mk|sh):[0-9]+'

# Markdown only: code comments legitimately cite a line when explaining a
# compiler message.
if [ "${#FILES[@]}" -gt 0 ]; then
  DOCS=("${FILES[@]}")
else
  mapfile -t DOCS < <(
    {
      git ls-files '*.md'
      echo 'AGENTS.md'
    } | sort -u | grep -v '^README\.md$' | grep -v '^vendor/'
  )
fi

FOUND=0
for f in "${DOCS[@]}"; do
  [ -f "$f" ] || continue
  while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    FOUND=$((FOUND + 1))
    printf '  LINE REF  %s:%s\n' "$f" "${hit%%:*}"
    printf '            %s\n' "$(printf '%s' "${hit#*:}" | sed 's/^[[:space:]]*//' | cut -c1-100)"
  done < <(grep -nE "$PATTERN" "$f" 2>/dev/null)
done

if [ "$FOUND" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] && echo "check-doc-refs: no line references in ${#DOCS[@]} documents"
  exit 0
fi

echo "check-doc-refs: $FOUND line reference(s) in documents."
echo "Name the symbol instead (function, type, constant), or the beads issue that"
echo "owns the defect. Line numbers rot; beads issues are snapshots with a date."
exit 1
