#!/usr/bin/env bash
#
# check-seam.sh - run the seam suite as a gate, and never skip it quietly.
#
# Usage: scripts/check-seam.sh
#
# The seam suite asserts what the X server sees: mapped, tiled, focused, tagged,
# and the EWMH root properties. It is the only check that reaches the map, tile
# and focus path, and before this gate it ran when the author or the reviewer
# remembered to run it. A defect on that path merged green twice.
#
# tests/x/run.sh starts Xephyr, which needs a parent display to draw into. That
# display is a private Xvfb this run starts, never the session's, so a gate run
# cannot put windows on the screen someone is working on and cannot answer
# differently because that screen is a different depth or wedged. It used to be
# xvfb-run only when DISPLAY was unset, which meant the local run was not the run
# CI performed. scripts/lib/private-display.sh owns the rule;
# WM_USE_SESSION_DISPLAY=1 is the documented way back to the session display.
# When no private display is possible the script fails and names what is
# missing, because a gate whose silence reads as a pass is worse than no gate.
#
# The baseline in tests/quality/seam-baseline.txt keeps the verdict meaningful:
# a failure listed there is known and does not fail the run, and a failure that
# is not listed does.
#
# Exit status: 0 when the suite has no new failure, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

case "${1:-}" in
  -h | --help)
    awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit 0
    ;;
  "") ;;
  *)
    echo "check-seam: unknown argument '$1'" >&2
    exit 1
    ;;
esac

if ! command -v Xephyr >/dev/null 2>&1; then
  echo "check-seam: Xephyr is not on PATH, so no nested server can start." >&2
  echo "check-seam: start the dev shell ('direnv allow' or 'nix develop') and run this again." >&2
  exit 1
fi

# shellcheck source=lib/private-display.sh
. "$ROOT_DIR/scripts/lib/private-display.sh"
wm_parent_display "$@" || exit 1

exec make test-x
