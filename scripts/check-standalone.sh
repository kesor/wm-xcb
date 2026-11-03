#!/usr/bin/env bash
#
# check-standalone.sh - the standalone build targets as the push gate runs them.
#
# The two targets link and run a test binary with no X connection. They print a
# gcc link line and one line per assertion, which is 215 lines on a passing run:
# the same wall the unit gate stopped printing, in the gate below it.
#
# Exit status: 0 when both targets build and run, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

# shellcheck source=scripts/lib/gate-log.sh
. scripts/lib/gate-log.sh

gate_log "check-standalone" make test-standalone || exit 1
gate_log "check-standalone" make test-sm-standalone || exit 1
echo "check-standalone: both standalone targets built and ran"
