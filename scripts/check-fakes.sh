#!/usr/bin/env bash
#
# check-fakes.sh - every stand-in for an outside program says which program it
# models, and that program is installed.
#
# Usage: scripts/check-fakes.sh [-v|--verbose]
#
# tests/fakes holds one executable per program the harnesses replace, and its
# second line is `# models: <program>, <what the real program does and what this
# one does instead>`. Nothing inside a test can check that claim: the stand-in
# answers every assertion the test asks it. Three fakes in this tree modelled
# `dmenu_run`, which backgrounds its pipeline and exits, while the launcher ran
# it and waited for a selection that never came; 1065 unit results and 148 seam
# results were green on the wrong program.
#
# So the claim is checked here instead, by the one thing the fakes cannot fake:
# the rest of PATH.
#
#   - The file is named after the program it stands in for, because the name is
#     how PATH shadowing finds it. A `dmenu` file claiming to model `dmenu_run`
#     shadows one program and describes another.
#   - The claimed program resolves on PATH with every fake directory removed. A
#     stand-in for a name nobody installed is a fake of an idea, and a name that
#     resolves only to the fake proves nothing.
#
# Unit tests that need a different body per case write their own fake; the
# models line is the same shape there, and the case that reads it back lives in
# tests/unit/test-launcher.c.
#
# Exit status: 0 when every stand-in passes, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAKE_DIR="$ROOT_DIR/tests/fakes"

VERBOSE=0
for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *)
      echo "check-fakes: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

if [ ! -d "$FAKE_DIR" ]; then
  echo "check-fakes: $FAKE_DIR is missing. The harnesses put it at the front of PATH;" >&2
  echo "             without it a launcher reaches the real program and the run measures that." >&2
  exit 1
fi

# The rest of PATH, with every copy of the fake directory dropped: a stand-in
# must not be the thing that satisfies its own claim.
FAKE_REAL=$(cd "$FAKE_DIR" && pwd -P)
REAL_PATH=""
while IFS= read -r entry; do
  [ -n "$entry" ] || continue
  entry_real=$(cd "$entry" 2>/dev/null && pwd -P) || continue
  [ "$entry_real" = "$FAKE_REAL" ] && continue
  REAL_PATH="${REAL_PATH:+$REAL_PATH:}$entry"
done < <(printf '%s\n' "$PATH" | tr ':' '\n')

if [ -z "$REAL_PATH" ]; then
  echo "check-fakes: PATH holds nothing but $FAKE_DIR; start the dev shell (direnv allow)" >&2
  exit 1
fi

FOUND=0
report() { # report <file> <problem>
  FOUND=$((FOUND + 1))
  printf '  %s: %s\n' "${1#"$ROOT_DIR/"}" "$2"
}

CHECKED=0
for fake in "$FAKE_DIR"/*; do
  [ -f "$fake" ] || continue
  CHECKED=$((CHECKED + 1))
  name=$(basename "$fake")

  if [ "$(sed -n 1p "$fake")" != '#!/bin/sh' ]; then
    report "$fake" "line 1 is not #!/bin/sh: the harness execs this file, and a shebang below the first line is not read"
    continue
  fi

  models=$(sed -n 2p "$fake")
  case "$models" in
    '# models: '*) ;;
    *)
      report "$fake" "line 2 is not '# models: <program>, <what it does>': name the program this stands in for"
      continue
      ;;
  esac

  claim=${models#'# models: '}
  program=${claim%%[ ,:;]*}
  if [ -z "$program" ] || [ "$program" = "$claim" ]; then
    report "$fake" "the # models: line names no program before its description"
    continue
  fi

  if [ "$program" != "$name" ]; then
    report "$fake" "models '$program' but is named '$name': PATH shadowing finds the file name, so this replaces one program and describes another"
    continue
  fi

  # Resolved with the fakes off PATH: command -v answers with the first match,
  # which is what the harness would exec without the stand-in.
  resolved=$(PATH="$REAL_PATH" command -v "$program" 2>/dev/null)
  if [ -z "$resolved" ]; then
    report "$fake" "models '$program', which is not on PATH outside $FAKE_DIR: a stand-in for a program nobody installed models an idea"
    continue
  fi

  [ "$VERBOSE" -eq 1 ] && printf '  ok    %s -> %s\n' "$name" "$resolved"
done

if [ "$CHECKED" -eq 0 ]; then
  echo "check-fakes: no stand-in in $FAKE_DIR; is this the repository root?" >&2
  exit 1
fi

if [ "$FOUND" -eq 0 ]; then
  echo "check-fakes: $CHECKED stand-in(s) name a program that is installed, and nothing else"
  exit 0
fi

echo "check-fakes: $FOUND stand-in(s) do not say what they stand in for."
echo "A fake answers every assertion the test asks it, so the program it names is"
echo "the only thing here that can be wrong without a test noticing."
exit 1
