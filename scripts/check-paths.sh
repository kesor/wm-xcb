#!/usr/bin/env bash
#
# check-paths.sh - fail on a path that only exists on one machine.
#
# Usage: scripts/check-paths.sh [--verbose]
#
# A committed file must not name somebody's home directory or their personal
# source layout. This is a public repository: nobody else has ~/src/ with the
# same content, so such a path is wrong for every other checkout, every worktree
# created elsewhere, and every CI runner. The check is cheap and runs on every
# commit; see docs/QUALITY.md.
#
# Flagged: /home/<user>/, /Users/<user>/, C:\Users\<user>\, and personal source
# trees under ~/src/ or ~/suckless/.
#
# Not flagged, on purpose: standard home locations such as ~/.config or
# ~/.agents, and /nix/store paths in Dockerfile and flake.nix, which describe a
# container image layout rather than a person's filesystem.
#
# Exit status: 0 when no tracked file names a home directory, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
case "${1:-}" in
  -v | --verbose) VERBOSE=1 ;;
  "") ;;
  *)
    echo "check-paths: unknown argument '$1'" >&2
    exit 1
    ;;
esac

# /home/<user>, /Users/<user>, C:\Users\<user>, and a personal source tree.
PATTERN='/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|[Cc]:\\Users\\|~/src/|~/suckless/'

# This script has to spell out the patterns it looks for, so it matches itself.
SELF="scripts/check-paths.sh"

FOUND=0
while IFS= read -r hit; do
  case "$hit" in
    "$SELF":*) continue ;;
  esac
  [ -n "$hit" ] || continue
  FOUND=$((FOUND + 1))
  printf '  HOME PATH  %s\n' "$hit"
done < <(git grep -nE "$PATTERN" -- . 2>/dev/null)

if [ "$FOUND" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] && echo "check-paths: no home directory paths in $(git ls-files | wc -l) tracked files"
  exit 0
fi

echo "check-paths: $FOUND line(s) name a home directory or personal source tree."
echo "Use a path relative to the repository, or a placeholder like <path-to-repo>."
exit 1
