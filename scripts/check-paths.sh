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
# container image layout rather than a person's filesystem. A directory a test
# builds for itself is not a person's filesystem either: the pattern in
# scripts/lib/path-report.sh matches a home only at the start of a field, so
# /tmp/home/.config is a fixture and /home/someone/.config is a finding.
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

# shellcheck source=lib/path-report.sh
. "$ROOT_DIR/scripts/lib/path-report.sh"

HITS=$(mktemp "${TMPDIR:-/tmp}/path-hits.XXXXXX") || exit 1
trap 'rm -f "$HITS"' EXIT

# 1 is "no match", which is the answer this gate wants. Anything above it is a
# search that did not run, and an empty result read as a clean tree is the one
# verdict a gate must never give.
git grep -nE "$PATH_PATTERN" -- . >"$HITS" 2>/dev/null
case $? in
  0 | 1) ;;
  *)
    echo "check-paths: git grep failed, so no tracked file was searched" >&2
    exit 1
    ;;
esac

path_verify "$HITS" check-paths || exit 1

[ "$VERBOSE" -eq 1 ] && echo "check-paths: no home directory paths in $(git ls-files | wc -l) tracked files"
exit 0
