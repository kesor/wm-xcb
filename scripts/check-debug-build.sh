#!/usr/bin/env bash
#
# check-debug-build.sh - the -pedantic debug build and its suite.
#
# This is the gate that caught the zero-length array and the event-type
# collisions, so it runs on every push. The leading clean stays: it is the only
# gate that builds the tree from scratch, which is what catches a missing
# dependency edge.
#
# Nothing here prints on a pass. A debug build is noisy by nature - every
# compiler line names the libxcb-errors include path - and printing it on every
# push buried the line that mattered and made a green gate look red.
#
# The build itself must print no diagnostic. The tree is warning-free, so a
# warning is the one line a person reads in a failing log, and gate_log would
# swallow it on a pass: the build is captured here and grepped instead.
#
# Exit status: 0 when the build, the suite and the final release build pass.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

# shellcheck source=scripts/lib/gate-log.sh
. scripts/lib/gate-log.sh

# Only this variant's directory: the point of the clean is a from-scratch debug
# build, and `make clean` removes every variant - which wiped the coverage and
# fuzz caches every push, turning two incremental gates (0.5s and 12s) back into
# full rebuilds (7s and 19s).
gate_log "check-debug-build" rm -rf build/debug "$(dirname "$0")/../wm" || exit 1

BUILD_LOG=$(mktemp) || exit 1
trap 'rm -f "$BUILD_LOG"' EXIT
if ! make DEBUG=1 >"$BUILD_LOG" 2>&1; then
  cat "$BUILD_LOG" >&2
  echo "check-debug-build: make DEBUG=1 failed; its output is above." >&2
  exit 1
fi
if grep -E ': (warning|error):' "$BUILD_LOG" >&2; then
  echo "check-debug-build: the -pedantic build printed the diagnostic above. The build is warning-free, so this one is new: fix the cause." >&2
  exit 1
fi

gate_log "check-debug-build" env DEBUG=1 ./scripts/check-unit.sh || exit 1
gate_log "check-debug-build" make -j"$(nproc)" || exit 1
echo "check-debug-build: debug build, its suite and the release build pass"
