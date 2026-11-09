#!/usr/bin/env bash
#
# check-bead-create-recipe.sh - fail when a document shows a `bd create` with no
# --acceptance.
#
# Usage: scripts/check-bead-create-recipe.sh [--verbose] [files...]
#
# `bd create` is the only write a worker makes to a bead's fields, and
# scripts/check-bead-acceptance.sh fails the next commit on a live bead that
# carries no acceptance criteria and that
# tests/quality/bead-acceptance-missing-baseline.txt does not record as debt. A
# recipe copied from a document that leaves the flag out therefore files a bead
# the same tree refuses, and the two ways out are the flag the recipe did not
# show and the baseline that records the bead as debt.
#
# The recipes are read from the tracked markdown, because a create command
# reaches a worker through a document and not through code. vendor/ and
# docs/archive/ are skipped for the reason scripts/check-docs.sh skips them.
#
# The line cannot be fixed once and left: `bd setup codex` writes
# .agents/skills/beads/SKILL.md from bd's own template, which has no
# --acceptance, so a hand edit there does not survive the command. This gate is
# what puts the flag back in front of whoever runs the setup next.
#
# A shell command runs past the end of its line, so a recipe is read as one
# command: a trailing backslash continues it, and so does a double-quoted
# argument left open, which is how a multi-line --description reads. Judging one
# line at a time would fail .agents/skills/wm-5s/SKILL.md, whose --acceptance
# sits further down its command than its `bd create`.
#
# A recipe is a command, not the words: `bd create` counts when it starts a line
# (after a shell prompt, if the document shows one) or follows a backtick, and
# the next thing on the line is a flag or a quoted title. That is what keeps a
# sentence about the command from being read as one, such as the line in
# AGENTS.md that ends at `bd create` and has no command after it.
#
# Exit status: 0 when every recipe carries --acceptance, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

VERBOSE=0
FILES=()
for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *) FILES+=("$arg") ;;
  esac
done

if [ "${#FILES[@]}" -gt 0 ]; then
  DOCS=("${FILES[@]}")
else
  mapfile -t DOCS < <(
    git ls-files '*.md' |
      grep -v '^vendor/' |
      grep -v '^docs/archive/'
  )
fi

awk -v verbose="$VERBOSE" '
function continues(line) {
  return length(line) > 0 && substr(line, length(line)) == "\\"
}

# An odd number of unescaped double quotes means an argument is still open, and
# the command runs onto the next line.
function quote_open(text,   i, n, c, open) {
  open = 0
  n = length(text)
  for (i = 1; i <= n; i++) {
    c = substr(text, i, 1)
    if (c == "\\") { i++; continue }
    if (c == "\"") open = !open
  }
  return open
}

# find_start <line> <from> - the column where a create command begins, or 0.
function find_start(line, from,   rest, off, p, nxt, before, prefix) {
  rest = substr(line, from)
  off = from
  while (match(rest, /bd[[:space:]]+create[[:space:]]+[^[:space:]]/)) {
    p = off + RSTART - 1
    nxt = substr(line, p + RLENGTH - 1, 1)
    before = (p > 1) ? substr(line, p - 1, 1) : ""
    prefix = substr(line, 1, p - 1)
    if ((before == "`" || prefix ~ /^[[:space:]]*(\$[[:space:]]+)?$/) && nxt ~ /[-"\\]/) return p
    off = p + RLENGTH
    rest = substr(line, off)
  }
  return 0
}

function judge(cmd, line,   shown) {
  if (index(cmd, "--acceptance") > 0) return
  # The help text is not a recipe: it files nothing, and the document a reader
  # copies from is not teaching a command to run there.
  if (cmd ~ /^bd[[:space:]]+create[[:space:]]+(-h|--help)([^[:alnum:]_-]|$)/) return
  found++
  shown = cmd
  sub(/\n.*/, "", shown)
  printf "  NO ACCEPTANCE  %s:%d\n", FILENAME, line
  printf "                 %s\n", substr(shown, 1, 100)
}

FNR == 1 { documents++; pending = 0; cmd = ""; cmdline = 0 }
{
  if (pending) {
    cmd = cmd "\n" $0
    if (continues($0) || quote_open(cmd)) next
    judge(cmd, cmdline)
    pending = 0
    cmd = ""
    next
  }
  start = find_start($0, 1)
  if (start == 0) next
  cmd = substr($0, start)
  cmdline = FNR
  if (continues($0) || quote_open(cmd)) {
    pending = 1
    next
  }
  judge(cmd, cmdline)
}
END {
  if (found > 0) {
    printf "check-bead-create-recipe: %d recipe(s) with no --acceptance.\n", found
    exit 1
  }
  if (verbose) printf "check-bead-create-recipe: %d document(s) clean\n", documents
}
' "${DOCS[@]}"
STATUS=$?
[ "$STATUS" -eq 0 ] && exit 0

cat <<'EOF'
A recipe a worker copies has to carry the flag, or the bead it files fails the
next commit. Write --acceptance with what a command will show when the work is
done, next to --description.

The skill under .agents/skills/beads/ is bd's own text: `bd setup codex` writes
it again from a template that has no --acceptance, so put the flag back in the
same commit that runs the setup.
EOF
exit 1
