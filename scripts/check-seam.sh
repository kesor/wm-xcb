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
# tests/x/run.sh starts Xephyr, which needs a host display to draw into. When
# there is none, the run goes under xvfb-run instead of being skipped: a gate
# whose silence reads as a pass is worse than no gate, and the CI runner is the
# machine that has no display and needs the check most. When neither a display
# nor xvfb-run is available the script fails and names what is missing.
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

if [ -z "${DISPLAY:-}" ]; then
  if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "check-seam: DISPLAY is unset and xvfb-run is not on PATH, so Xephyr has" >&2
    echo "check-seam: no host display to draw into. Set DISPLAY or install xvfb-run." >&2
    exit 1
  fi
  echo "check-seam: DISPLAY is unset, running the seam suite under xvfb-run"
  exec xvfb-run -a "$0"
fi

exec make test-x
