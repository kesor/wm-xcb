#!/usr/bin/env bash
#
# check-debug-build.sh - the -pedantic debug build and its suite.
#
# This is the gate that caught the zero-length array and the event-type
# collisions, so it runs on every push. The leading clean stays: it is the only
# gate that builds the tree from scratch, which is what catches a missing
# dependency edge.
#
# Nothing here prints on a pass. A debug build is noisy by nature - 13 warnings
# the -pedantic flag is there to find - and printing them on every push buried
# the one that mattered and made a green gate look red.
#
# Exit status: 0 when the build, the suite and the final release build pass.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

# shellcheck source=scripts/lib/gate-log.sh
. scripts/lib/gate-log.sh

gate_log "check-debug-build" make clean || exit 1
gate_log "check-debug-build" make DEBUG=1 || exit 1
gate_log "check-debug-build" env DEBUG=1 ./scripts/check-unit.sh || exit 1
gate_log "check-debug-build" make -j"$(nproc)" || exit 1
echo "check-debug-build: debug build, its suite and the release build pass"
