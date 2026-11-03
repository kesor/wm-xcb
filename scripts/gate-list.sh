#!/usr/bin/env bash
#
# gate-list.sh - the gates each hook runs, read out of lefthook.yml.
#
# Usage: scripts/gate-list.sh [hook]
#
# lefthook.yml is the one list of gates: the hooks in .githooks/ run it, CI runs
# it with --force, and every document that wants to say what runs when points
# here instead of keeping a copy. Copies drift - a header comment in that file
# claimed a pre-push set missing four of the gates that run, and a skill carried
# a gate table that still said `make test` a commit after the hook switched to
# scripts/check-unit.sh. docs/QUALITY.md carries the same drift in its layers
# table; it points here now.
#
# Exit status: 0, or 1 when the file names no hook at all, which means the file
# changed shape and this list would be a lie.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="$ROOT_DIR/lefthook.yml"
want=${1:-}

hooks=$(grep -cE '^[a-z][a-z-]*:[[:space:]]*$' "$CONFIG" || true)
if [ "${hooks:-0}" -eq 0 ]; then
  echo "gate-list: no hooks found in $CONFIG; the file changed shape and this list cannot be trusted" >&2
  exit 1
fi

awk -v want="$want" '
  /^[a-z][a-z-]*:[[:space:]]*$/ {
    hook = $0; sub(/:.*/, "", hook)
    printing = (want == "" || hook == want)
    if (printing) print hook ":"
    next
  }
  printing && /^    [a-z0-9][a-z0-9-]*:[[:space:]]*$/ {
    name = $0; gsub(/^[[:space:]]+|[[:space:]]*:$/, "", name); print "  " name
  }
' "$CONFIG"
