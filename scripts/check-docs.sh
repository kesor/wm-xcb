#!/usr/bin/env bash
#
# check-docs.sh - fail when a document duplicates a fact that has a command.
#
# Usage: scripts/check-docs.sh [files...]
#
# Three kinds of rot, all cheap to prevent and expensive to live with:
#
#   1. Line references. `wm-xcb.c:212` is wrong the moment a line is added above
#      it, so every code change becomes prose edits in files that have nothing
#      to do with the change.
#   2. Counts of things a command already reports: assertions, tests, violations,
#      components, lines. The number in the document is a second source of truth
#      that nobody updates, and updating it is not the fix: deleting it is.
#   3. Completion percentages. "~75% complete" counts whatever the author
#      happened to count (files written, not behaviour shown), and it reads as a
#      measurement, so it is believed. Status comes from `bd list --type=epic`
#      and from the test commands; a percentage of a plan is not either.
#
# Documents say what shape the system has; the command says how much. Beads
# issues may cite lines and counts freely, because they are snapshots with a
# date on them.
#
# Not scanned: docs/archive/ (frozen history) and vendor/.
#
# Exit status: 0 when the documents are clean, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
FILES=()
for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *) FILES+=("$arg") ;;
  esac
done

if [ "${#FILES[@]}" -gt 0 ]; then
  DOCS=("${FILES[@]}")
else
  mapfile -t DOCS < <(
    {
      git ls-files '*.md'
      echo 'AGENTS.md'
    } | sort -u |
      grep -v '^docs/archive/' |
      grep -v '^vendor/'
  )
fi

# 1. line references in any document
LINE_REF='\b[A-Za-z0-9_./-]+\.(c|h|yml|yaml|nix|mk|sh):[0-9]+'

# 2. counts that duplicate a command. A leading section mark is not a count,
# and neither is the value of an assignment: `make DEBUG=1 test-x` is a
# command, not "1 test". One optional token between the number and the noun,
# so "18 `test-*.c` files" is caught as well as "651 assertions". Spelled-out
# numbers are left alone: "four lines" and "one component" describe shape, and
# the false positives cost more than the rare stale "eight components".
#
# The separator is a space or a hyphen: "375-line keymap dump" claims the same
# count as "375 lines", and the hyphenated form passed the gate untouched. A
# hyphenated count has to start a word, because a file name such as
# `test-94-lines.sh` carries a number, a hyphen and a noun without claiming
# how many of anything there are.
NUMBER='(^|[^§0-9.=])'
WORD_START='(^|[^§0-9.=[:alnum:]_-])'
NOUNS='(assertions?|tests?|files?|lines?|violations?|waived|components?|bindings?|epics?)'
COUNT="((${NUMBER})[0-9][0-9,]* ([\`'\"A-Za-z0-9_*.-]+ )?|(${WORD_START})[0-9][0-9,]*-)${NOUNS}\b"

# 3. completion percentages. Position strings ("50% 50% 50% 50%") and CPU
# figures ("100% of a core") are not completion claims and must not match, hence
# the trailing verb rather than a bare `%`.
PERCENT='(^|[^0-9.])(~|about |approximately )?[0-9]{1,3}([.,][0-9]+)? ?% ?(complete|completion|done|finished|implemented)|percent (complete|done|finished)'

FOUND=0
report() { # report <file> <line> <kind> <text>
  FOUND=$((FOUND + 1))
  printf '  %-11s %s:%s\n' "$3" "$1" "$2"
  printf '              %s\n' "$(printf '%s' "$4" | sed 's/^[[:space:]]*//' | cut -c1-100)"
}

for f in "${DOCS[@]}"; do
  [ -f "$f" ] || continue
  while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    report "$f" "${hit%%:*}" "LINE REF" "${hit#*:}"
  done < <(grep -nE "$LINE_REF" "$f" 2>/dev/null)
  while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    report "$f" "${hit%%:*}" "COUNT" "${hit#*:}"
  done < <(grep -nE "$COUNT" "$f" 2>/dev/null)
  while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    report "$f" "${hit%%:*}" "PERCENT" "${hit#*:}"
  done < <(grep -niE "$PERCENT" "$f" 2>/dev/null)
done

if [ "$FOUND" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] && echo "check-docs: ${#DOCS[@]} document(s) clean"
  exit 0
fi

echo "check-docs: $FOUND problem(s) in documents."
echo "Line numbers rot, counts drift, and a percentage of a plan is a guess with"
echo "a unit: name the symbol or the beads issue that owns the defect, and let the"
echo "command print the number."
exit 1
