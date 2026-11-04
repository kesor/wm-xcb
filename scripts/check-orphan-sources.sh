#!/usr/bin/env bash
#
# check-orphan-sources.sh - fail when a tracked C file is in no build target.
#
# Usage: scripts/check-orphan-sources.sh [--verbose]
#
# A .c file that is tracked but missing from SRC is invisible twice: the
# compiler never builds it, and scripts/check-async.sh scans
# `make -s print-src`, so a blocking call in it is never reported. wm-kbd-mouse.c
# sat that way with an xcb_grab_pointer_reply() in it, and the async audit could
# only say so by reading the tree by hand.
#
# The tracked list is git's .c files outside tests/ and tools/, which have their
# own targets. The built list is `make -s print-src`, the same list the build and
# the async gate use.
#
# Exit status: 0 when every tracked source is built, 1 otherwise.

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
    echo "check-orphan-sources: unknown argument '$1'" >&2
    exit 1
    ;;
esac

# shellcheck source=lib/orphan-sources-report.sh
. "$ROOT_DIR/scripts/lib/orphan-sources-report.sh"

TRACKED=$(mktemp "${TMPDIR:-/tmp}/orphan-sources-tracked.XXXXXX")
RAW=$(mktemp "${TMPDIR:-/tmp}/orphan-sources-raw.XXXXXX")
BUILT=$(mktemp "${TMPDIR:-/tmp}/orphan-sources-built.XXXXXX")
trap 'rm -f "$TRACKED" "$RAW" "$BUILT"' EXIT

if ! git ls-files '*.c' >"$TRACKED"; then
  echo "check-orphan-sources: could not list the tracked sources; git ls-files failed" >&2
  exit 1
fi

if ! make -s print-src >"$RAW" 2>/dev/null; then
  echo "check-orphan-sources: could not read SRC from 'make -s print-src'" >&2
  exit 1
fi
# SRC is one space-separated line; a path that is not a .c file is not a source
# and would otherwise be reported as a built file that nobody tracks.
tr ' ' '\n' <"$RAW" | grep -E '\.c$' >"$BUILT" || true

ORPHAN_SOURCES_VERBOSE=$VERBOSE orphan_sources_verify "$TRACKED" "$BUILT" check-orphan-sources
