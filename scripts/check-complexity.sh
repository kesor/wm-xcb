#!/usr/bin/env bash
#
# check-complexity.sh - rate each function's cognitive complexity, keep a floor.
#
# Usage: scripts/check-complexity.sh [--update-baseline] [files...]
#
# Nothing else in this tree says how much there is to test. Coverage says what
# the tests execute; this says how many decisions a function asks them to reach.
# A function at 45 is one where a missing branch is invisible and a wrong one is
# easy to write.
#
# clang-tidy's readability-function-cognitive-complexity reports a function name
# and a number, so the baseline is a list of names rather than of line numbers
# that move under every edit. The check needs a compile database; when the tree
# has none, this builds one.
#
# A function above its recorded number fails, and a function at or above the
# floor with no entry fails, so a new one cannot arrive unnoticed. --update
# rewrites the baseline; say in the commit why a number went up.
#
# Exit status: 0 when every function is within its entry, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BASELINE="tests/quality/complexity-baseline.txt"
FLOOR=11
UPDATE=0
files=()
for arg in "$@"; do
  case $arg in
    --update-baseline) UPDATE=1 ;;
    --floor=*) FLOOR=${arg#--floor=} ;;
    -*) echo "check-complexity: unknown argument '$arg'" >&2; exit 1 ;;
    *) files+=("$arg") ;;
  esac
done

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "check-complexity: clang-tidy is not on PATH. Start the dev shell." >&2
  exit 1
fi

# The number a function gets depends on the clang-tidy that counted it: a
# condition two builds of the same check read differently is two points on a
# function. A gate that fails on a tool difference is a gate nobody trusts, so
# the version is recorded with the baseline and named when one is missing.
TOOL=$(clang-tidy --version 2>/dev/null | sed -n 's/^ *LLVM version \(.*\)/\1/p' | head -1)
TOOL=${TOOL:-unknown}

if [ "${#files[@]}" -eq 0 ]; then
  mapfile -t files < <(make -s print-src | tr ' ' '\n' | grep -v '^wm\.c$' | grep -v '^$')
fi

# clang-tidy wants a compile database. `make compile-commands` uses bear, which
# does a full clean build; that is minutes, and this gate runs on every commit.
# One entry per source with the flags the Makefile already writes to
# compile_flags.txt is the same information in under a second.
if [ ! -f compile_flags.txt ]; then
  make -s compile_flags.txt >/dev/null 2>&1 || true
fi
if [ ! -f compile_flags.txt ]; then
  echo "check-complexity: no compile_flags.txt; run 'make' once so the flags exist" >&2
  exit 1
fi
python3 - "$ROOT_DIR" "${files[@]}" <<'PY'
import json, os, sys
root, files = sys.argv[1], sys.argv[2:]
flags = open(os.path.join(root, "compile_flags.txt")).read().strip()
entries = [
    {"directory": root, "file": os.path.join(root, f), "command": f"clang {flags} -c {f}"}
    for f in files
]
with open(os.path.join(root, "compile_commands.json"), "w") as fh:
    json.dump(entries, fh)
PY

measured=$(clang-tidy -p . \
  --checks="-*,readability-function-cognitive-complexity" \
  --config="{CheckOptions: {readability-function-cognitive-complexity.Threshold: 1}}" \
  "${files[@]}" 2>/dev/null |
  grep -oE "function '[^']+' has cognitive complexity of [0-9]+" |
  sed -E "s/function '([^']+)' has cognitive complexity of ([0-9]+)/\1 \2/" |
  sort)

if [ -z "$measured" ]; then
  echo "check-complexity: clang-tidy reported nothing; the check or the database is wrong" >&2
  exit 1
fi

if [ "$UPDATE" -eq 1 ]; then
  {
    echo "# Cognitive complexity per function, from clang-tidy $TOOL."
    echo "# Written by scripts/check-complexity.sh --update-baseline."
    echo "# Functions below $FLOOR are not listed. A function above its number fails"
    echo "# the gate; a function at or above $FLOOR with no entry fails it too, which"
    echo "# is the moment somebody decides the number is acceptable."
    echo "#"
  } >"$BASELINE"
  printf '%s\n' "$measured" | awk -v floor="$FLOOR" '$2 >= floor' >>"$BASELINE"
  echo "check-complexity: wrote $(awk -v f="$FLOOR" '!/^#/ && $2>=f' "$BASELINE" | wc -l) entries to $BASELINE"
  exit 0
fi

if [ ! -f "$BASELINE" ]; then
  echo "check-complexity: $BASELINE is missing; run --update-baseline once and commit it" >&2
  exit 1
fi

recorded_tool=$(sed -n "s/^# Cognitive complexity per function, from clang-tidy \([^.]*\).*/\1/p" "$BASELINE" | head -1)
if [ -n "$recorded_tool" ] && [ "$recorded_tool" != "$TOOL" ]; then
  echo "check-complexity: the baseline was measured with clang-tidy $recorded_tool and this is $TOOL; the numbers can differ by a point or two per function. Regenerate it in this environment if the gate fails on a difference you did not write." >&2
fi

problems=0
while read -r name value; do
  [ -z "${name:-}" ] && continue
  case $name in \#*) continue ;; esac
  [ "${value:-0}" -ge "$FLOOR" ] || continue
  recorded=$(awk -v n="$name" '$1 == n { print $2 }' "$BASELINE")
  if [ -z "$recorded" ]; then
    echo "check-complexity: $name is at $value and has no baseline entry (floor $FLOOR)" >&2
    problems=$((problems + 1))
  elif [ "$value" -gt "$recorded" ]; then
    echo "check-complexity: $name went from $recorded to $value" >&2
    problems=$((problems + 1))
  fi
done <<<"$measured"

if [ "$problems" -gt 0 ]; then
  echo "check-complexity: $problems function(s) above the baseline (clang-tidy $TOOL). Split the function along a decision that already exists, or run --update-baseline and say why in the commit." >&2
  exit 1
fi
echo "check-complexity: every function at or above $FLOOR is within its baseline entry"
