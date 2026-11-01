#!/usr/bin/env bash
#
# format-check.sh - fail when a C file is not clang-format clean.
#
# Usage:
#   scripts/format-check.sh [files...]     # checks the files given
#   scripts/format-check.sh                # checks every built source and test
#
# This is a check, not a fixer: `make format` rewrites files, this one reports
# and exits non-zero, so a commit hook cannot silently change what is staged.
#
# Exit status: 0 when every file is formatted, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "format-check: clang-format not found; start the dev shell (direnv allow)" >&2
  exit 1
fi

if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  mapfile -t FILES < <(
    {
      make -s print-src 2>/dev/null | tr ' ' '\n'
      echo "test-runner.h"
      ls test-*.c test-*.h 2>/dev/null
      echo "tools/xclient.c"
    } | grep -E '\.(c|h)$' | sort -u
  )
fi

FAILED=0
CHECKED=0
for f in "${FILES[@]}"; do
  [ -f "$f" ] || continue
  case "$f" in
    vendor/*) continue ;;
  esac
  CHECKED=$((CHECKED + 1))
  if ! diff=$(clang-format --style=file --dry-run --Werror "$f" 2>&1); then
    FAILED=$((FAILED + 1))
    printf '  needs formatting: %s\n' "$f"
    printf '%s\n' "$diff" | head -5 | sed 's/^/    /'
  fi
done

if [ "$FAILED" -gt 0 ]; then
  echo "format-check: $FAILED of $CHECKED file(s) need formatting. Run: make format"
  exit 1
fi
echo "format-check: $CHECKED file(s) clean"
