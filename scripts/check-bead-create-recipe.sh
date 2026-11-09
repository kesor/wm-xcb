#!/usr/bin/env bash
#
# check-bead-create-recipe.sh - fail when a document shows a `bd create` with no
# --acceptance.
#
# Usage: scripts/check-bead-create-recipe.sh [--verbose] [files...]
#
# `bd create` is the only write a worker makes to the acceptance field, and
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
# command: a trailing backslash continues it, unless that backslash is itself
# escaped, and so does a quoted argument left open, single or double, which is
# how a multi-line --description reads. Judging one line at a time would fail
# .agents/skills/wm-5s/SKILL.md, whose --acceptance sits further down its
# command than its `bd create`. A command still open at the end of a file is
# judged there; dropping it reports the file clean.
#
# --acceptance counts when it is the option, a token of its own or
# --acceptance=.... The word in a description, a comment, or a longer flag name
# does not: a search for the letters would pass the recipe the gate exists to
# catch. `bd create` counts when it starts a line (after a `$` prompt, if the
# document shows one) or follows a backtick, and the next thing on the line is
# a flag or a quoted title, single or double. That is what keeps a sentence
# about the command from being read as one, such as the line in AGENTS.md that
# ends at `bd create` and has no command after it.
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
BEGIN { SQ = sprintf("%c", 39) }

# A trailing backslash continues the command only when it is not itself escaped.
# The last character is a backslash either way; an even count is a finished
# command, and treating it as open swallows the next recipe into this one.
function continues(line,   n, i) {
  n = length(line)
  i = 0
  while (n - i >= 1 && substr(line, n - i, 1) == "\\") i++
  return i % 2 == 1
}

# An argument left open, single or double, continues the command. A quote of
# the other kind inside it is text: a double quote inside single quotes used to
# look open, and the rest of the file was then one command.
function quote_open(text,   i, n, c, in_d, in_s) {
  n = length(text)
  i = 1
  while (i <= n) {
    c = substr(text, i, 1)
    if (in_s) {
      if (c == SQ) in_s = 0
      i++
      continue
    }
    if (c == "\\") { i += 2; continue }
    if (in_d) {
      if (c == "\"") in_d = 0
      i++
      continue
    }
    if (c == SQ) { in_s = 1; i++; continue }
    if (c == "\"") { in_d = 1; i++; continue }
    i++
  }
  return in_d || in_s
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
    if ((before == "`" || prefix ~ /^[[:space:]]*(\$[[:space:]]+)?$/) &&
        (nxt == "-" || nxt == "\"" || nxt == SQ || nxt == "\\")) return p
    off = p + RLENGTH
    rest = substr(line, off)
  }
  return 0
}

# The option is a token outside quotes. A search for the letters matches the
# word in a description, a comment, or a longer flag name, and the recipe with
# no option then passes.
function has_acceptance(cmd,   i, n, c, in_d, in_s, tok) {
  n = length(cmd)
  i = 1
  while (i <= n) {
    c = substr(cmd, i, 1)
    if (in_s) {
      if (c == SQ) in_s = 0
      i++
      continue
    }
    if (in_d) {
      if (c == "\\") { i += 2; continue }
      if (c == "\"") in_d = 0
      i++
      continue
    }
    if (c == "\\") { i += 2; continue }
    if (c == SQ) { in_s = 1; i++; continue }
    if (c == "\"") { in_d = 1; i++; continue }
    if (c == "#") {
      while (i <= n && substr(cmd, i, 1) != "\n") i++
      continue
    }
    if (c == " " || c == "\t" || c == "\n" || c == "\r") { i++; continue }
    tok = ""
    while (i <= n) {
      c = substr(cmd, i, 1)
      if (c == " " || c == "\t" || c == "\n" || c == "\r" || c == "#" || c == "\"" || c == SQ) break
      if (c == "\\") {
        i++
        if (i <= n && substr(cmd, i, 1) != "\n") tok = tok substr(cmd, i, 1)
        i++
        continue
      }
      tok = tok c
      i++
    }
    if (tok == "--acceptance" || substr(tok, 1, 14) == "--acceptance=") return 1
  }
  return 0
}

function judge(cmd, line, file,   shown) {
  # The help text is not a recipe: it files nothing, and the document a reader
  # copies from is not teaching a command to run there.
  if (cmd ~ /^bd[[:space:]]+create[[:space:]]+(-h|--help)([^[:alnum:]_-]|$)/) return
  if (has_acceptance(cmd)) return
  found++
  shown = cmd
  sub(/\n.*/, "", shown)
  printf "  NO ACCEPTANCE  %s:%d\n", file, line
  printf "                 %s\n", substr(shown, 1, 100)
}

function flush_pending() {
  if (!pending) return
  judge(cmd, cmdline, cmdfile)
  pending = 0
  cmd = ""
}

FNR == 1 {
  flush_pending()
  documents++
  pending = 0
  cmd = ""
  cmdline = 0
  cmdfile = ""
}
{
  if (pending) {
    cmd = cmd "\n" $0
    if (continues($0) || quote_open(cmd)) next
    judge(cmd, cmdline, cmdfile)
    pending = 0
    cmd = ""
    next
  }
  start = find_start($0, 1)
  if (start == 0) next
  cmd = substr($0, start)
  cmdline = FNR
  cmdfile = FILENAME
  if (continues($0) || quote_open(cmd)) {
    pending = 1
    next
  }
  judge(cmd, cmdline, cmdfile)
}
END {
  flush_pending()
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
