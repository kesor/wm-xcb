#!/usr/bin/env bash
#
# check-bead-refs.sh - fail when code cites a tracker id instead of saying the thing.
#
# Usage: scripts/check-bead-refs.sh [--verbose]
#
# A comment that says "see <bead-id>" is empty to anyone reading the code: they
# do not have the tracker open, and the id carries no explanation. The code has
# to hold the reason, the constraint or the workaround itself. The same goes for
# a script's error message and a Makefile's fail_text: someone reading a failure
# should not have to run bd to understand it.
#
# Documents are different: a design or workflow document may name the tracker,
# because pointing at the tracker is its subject. Source, scripts, build files
# and CI configs are not documents.
#
# The ids are read from the tracker rather than hardcoded, so the check cannot go
# stale, and it skips cleanly where there is no database (CI, a fresh clone).
#
# Exit status: 0 when no code file cites a bead, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
case "${1:-}" in
  -v | --verbose) VERBOSE=1 ;;
  -h | --help)
    sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  "") ;;
  *)
    echo "check-bead-refs: unknown argument '$1'" >&2
    exit 1
    ;;
esac

need() { command -v "$1" >/dev/null 2>&1; }

if ! need bd || ! need jq; then
  echo "check-bead-refs: bd or jq missing, cannot read the ids; skipping"
  exit 0
fi

mapfile -t IDS < <(bd list --json 2>/dev/null | jq -r '.[].id // empty' 2>/dev/null || true)
if [ "${#IDS[@]}" -eq 0 ]; then
  echo "check-bead-refs: no beads database in this checkout; skipping"
  exit 0
fi

# One alternation, dots escaped, longest first so a child id is not read as
# its parent followed by a digit.
PATTERN=$(printf '%s\n' "${IDS[@]}" | sed 's/\./\\./g' | awk '{ print length, $0 }' |
  sort -rn | cut -d' ' -f2- | paste -sd'|')
PATTERN="\\b(${PATTERN})\\b"

mapfile -t FILES < <(git ls-files '*.c' '*.h' '*.sh' '*.yml' '*.yaml' 'Makefile' | grep -v '^vendor/')

FOUND=0
for f in "${FILES[@]}"; do
  [ -f "$f" ] || continue
  while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    FOUND=$((FOUND + 1))
    printf '  BEAD REF  %s:%s\n' "$f" "${hit%%:*}"
    printf '            %s\n' "$(printf '%s' "${hit#*:}" | sed 's/^[[:space:]]*//' | cut -c1-100)"
  done < <(grep -nE "$PATTERN" "$f" 2>/dev/null)
done

if [ "$FOUND" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] && echo "check-bead-refs: no tracker ids in ${#FILES[@]} code files"
  exit 0
fi

echo "check-bead-refs: $FOUND reference(s) to a beads issue in code."
echo "Say the reason, the constraint or the workaround in place: a reader of the"
echo "file cannot follow an id to another system."
exit 1
