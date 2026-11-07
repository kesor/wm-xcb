#!/usr/bin/env bash
#
# check-root.sh - fail when a tracked file sits at the repository root.
#
# Usage: scripts/check-root.sh [--verbose]
#
# The root held the hub, the XCB connection, the state machine framework, the
# logger and the configuration beside the build files. A reader opening the tree
# saw implementation where an index should be, and every new file had to guess
# where it went. The allowlist in scripts/lib/root-report.sh is the shape the
# root keeps: build files, documentation entry points, and the directories that
# group the code.
#
# The list comes from git's index, so a file being committed is a finding and an
# untracked scratch file is not. An empty list is a failed `git ls-files`, not a
# tidy root, and fails.
#
# Exit status: 0 when every root entry is allowed, 1 otherwise.

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
    echo "check-root: unknown argument '$1'" >&2
    exit 1
    ;;
esac

# shellcheck source=lib/root-report.sh
. "$ROOT_DIR/scripts/lib/root-report.sh"

ENTRIES=$(mktemp "${TMPDIR:-/tmp}/root-entries.XXXXXX")
trap 'rm -f "$ENTRIES"' EXIT

# Every tracked path collapsed to its top-level component: files keep their
# name, anything under a directory contributes the directory once.
if ! git ls-files | sed 's|/.*||' | sort -u >"$ENTRIES"; then
  echo "check-root: could not list the tracked files; git ls-files failed" >&2
  exit 1
fi

ROOT_VERBOSE=$VERBOSE root_verify "$ENTRIES" check-root
