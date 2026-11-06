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
# merges the measured numbers into the baseline; say in the commit why a number
# went up.
#
# A file under tests/ is not measured. clang-tidy charges each ASSERT's if/else
# as a decision, so a test body's number is its assertion count, and the remedy
# this gate offers - split along a decision - would mean deleting assertions.
# Coverage floors cover production sources only for the same reason: a number
# nobody can act on teaches people to edit the baseline by hand instead.
#
# Exit status: 0 when every function is within its entry, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BASELINE="tests/quality/complexity-baseline.txt"
FLOOR=11
UPDATE=0
ADVISORY=0
files=()
for arg in "$@"; do
  case $arg in
    --update-baseline) UPDATE=1 ;;
    --floor=*) FLOOR=${arg#--floor=} ;;
    --advisory) ADVISORY=1 ;;
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

skipped_tests=0
kept=()
for f in "${files[@]}"; do
  case ${f#./} in
    tests/*) skipped_tests=$((skipped_tests + 1)) ;;
    *) kept+=("$f") ;;
  esac
done
files=()
if [ "${#kept[@]}" -gt 0 ]; then
  files=("${kept[@]}")
fi

if [ "${#files[@]}" -eq 0 ]; then
  echo "check-complexity: $skipped_tests test source(s), nothing for this gate to measure"
  exit 0
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
  # clang-tidy measures nothing in a header it is handed directly, so a commit
  # that touches only headers measures nothing. Failing it sent workers to
  # --update-baseline with the wrong cause. The guard is for the other case: a
  # .c that produced no measurement is a broken compile database, not a pass.
  # The message says "not measured", not "no function body": config.wm.def.h
  # defines config_wire_keybindings and is still skipped, so a body can be here.
  if printf '%s\n' "${files[@]}" | grep -qv '\.h$'; then
    echo "check-complexity: clang-tidy reported nothing; the check or the database is wrong" >&2
    exit 1
  fi
  echo "check-complexity: ${#files[@]} header(s), nothing for this gate to measure"
  exit 0
fi

if [ "$UPDATE" -eq 1 ]; then
  # Merge into the baseline; do not rewrite it from the files in hand. The
  # failure message tells a worker to run this for the files the commit changes,
  # and a rewrite from those deleted every entry it did not measure - 28
  # production entries went, and the next push failed on all of them. An entry
  # whose function was measured and is now below the floor is dropped, so a
  # split shows.
  previous=$(awk '!/^#/ && NF { print $1, $2 }' "$BASELINE" 2>/dev/null)
  merged=$({
    printf '%s\n' "$previous"
    printf '%s\n' "$measured"
  } | awk -v floor="$FLOOR" '!/^#/ && NF { value[$1] = $2 } END { for (n in value) if (value[n] >= floor) print n, value[n] }' | sort)
  {
    echo "# Cognitive complexity per function, from clang-tidy $TOOL."
    echo "# Written by scripts/check-complexity.sh --update-baseline."
    echo "# Functions below $FLOOR are not listed. A function above its number fails"
    echo "# the gate; a function at or above $FLOOR with no entry fails it too, which"
    echo "# is the moment somebody decides the number is acceptable."
    echo "#"
    echo "# A file under tests/ is not measured: a test body's number is its assertion"
    echo "# count, and no split of a test keeps the assertions."
    printf '%s\n' "$merged"
  } >"$BASELINE"
  echo "check-complexity: wrote $(awk -v f="$FLOOR" '!/^#/ && $2>=f' "$BASELINE" | wc -l) entries to $BASELINE, $(printf '%s\n' "$measured" | awk -v f="$FLOOR" '$2>=f' | wc -l) measured in this run"
  exit 0
fi

if [ ! -f "$BASELINE" ]; then
  echo "check-complexity: $BASELINE is missing; run --update-baseline once and commit it" >&2
  exit 1
fi

recorded_tool=$(sed -n "s/.*from clang-tidy \(.*\)\.$/\1/p" "$BASELINE" | head -1)
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
  echo "check-complexity: $problems function(s) above the baseline (clang-tidy $TOOL). Split the function along a decision that already exists, or run --update-baseline <the files this commit changes> and say why in the commit." >&2
  if [ "$ADVISORY" -eq 1 ]; then
    # The number is a heuristic and it differs between environments: the same
    # tree and the same clang-tidy reported mod_key_name at 14 here and 38 on a
    # CI runner, which is not a difference of one condition. Until that is
    # understood, CI prints this and does not fail on it, while the hook gates
    # fail on the machine where the baseline was measured.
    echo "check-complexity: --advisory, so this does not fail the run" >&2
    exit 0
  fi
  exit 1
fi
echo "check-complexity: every function at or above $FLOOR is within its baseline entry"
