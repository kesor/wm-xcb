#!/usr/bin/env bash
#
# format-check.sh - check or fix clang-format style for every C file.
#
# Usage:
#   scripts/format-check.sh            # check the files this gate owns
#   scripts/format-check.sh --fix      # rewrite them in place. Same list.
#   scripts/format-check.sh --list     # print that list and exit
#   scripts/format-check.sh file.c ...  # check only the files given
#
# The check and the fixer must agree on which files they own, or `make format`
# rewrites a file the gate does not look at. That is what happened with
# config.wm.def.h: the old `make format` reformatted it, the gate never checked
# it. One list, in this file, used by both. --list is that list on stdout, which
# is how tests/x/test-98-format-list.sh pins what the two of them cover.
#
# Exit status: 0 when every file is formatted (or after --fix), 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

FIX=0
LIST=0
FILES=()
for arg in "$@"; do
  case "$arg" in
    -f | --fix) FIX=1 ;;
    --list) LIST=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *) FILES+=("$arg") ;;
  esac
done

if ! command -v clang-format >/dev/null 2>&1; then
  echo "format-check: clang-format not found; start the dev shell (direnv allow)" >&2
  exit 1
fi

# Everything tracked or about to be, minus vendor. vendor/ is third-party and
# not ours to restyle. Untracked files are in the list because a new source or
# test file is what the next commit adds: a list built from the index alone
# reports the tree formatted while the file nobody has read is not, and the
# pre-commit gate then rejects the commit that `make format` said was clean.
if [ "${#FILES[@]}" -eq 0 ]; then
  mapfile -t FILES < <(git ls-files --cached --others --exclude-standard '*.c' '*.h' | grep -v '^vendor/')
fi

if [ "$LIST" -eq 1 ]; then
  printf '%s\n' "${FILES[@]}"
  exit 0
fi

FAILED=0
CHECKED=0
for f in "${FILES[@]}"; do
  [ -f "$f" ] || continue
  case "$f" in
    vendor/*) continue ;;
  esac
  CHECKED=$((CHECKED + 1))

  if [ "$FIX" -eq 1 ]; then
    clang-format --style=file -i "$f" || FAILED=$((FAILED + 1))
    continue
  fi

  if ! diff=$(clang-format --style=file --dry-run --Werror "$f" 2>&1); then
    FAILED=$((FAILED + 1))
    printf '  needs formatting: %s\n' "$f"
    printf '%s\n' "$diff" | head -5 | sed 's/^/    /'
  fi
done

if [ "$FIX" -eq 1 ]; then
  if [ "$FAILED" -gt 0 ]; then
    echo "format-check: clang-format failed on $FAILED file(s)"
    exit 1
  fi
  echo "format-check: formatted $CHECKED file(s)"
  exit 0
fi

if [ "$FAILED" -gt 0 ]; then
  echo "format-check: $FAILED of $CHECKED file(s) need formatting. Run: make format"
  exit 1
fi
echo "format-check: $CHECKED file(s) clean"
