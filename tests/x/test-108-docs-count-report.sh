#!/usr/bin/env bash
# A count written in a document is a second source of truth: the command that
# measures it prints the number, and the document drifts. check-docs.sh caught
# "651 assertions" and passed "375-line keymap dump", which claims the same
# count with a hyphen, so a wrong number stayed in a document while the gate
# reported it clean. The shapes pinned here are the ones a regex over prose has
# to tell apart:
#
#   - the space form, which already failed, and the hyphen form, which did not;
#   - a hyphen that belongs to a file name and not to a count: the number in
#     `test-94-fresh-wm-per-file.sh` says nothing about how many files exist;
#   - a range, whose second number is a count next to a hyphen ("5-10 files");
#   - a date and a terminal name, which carry digits and hyphens and are not
#     counts of anything.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/docs-count"
rm -rf "$DIR"
mkdir -p "$DIR"

# doc <name> <line> - writes a one-line document and prints its path. The line
# is the third line of the file so the report's line number is worth asserting.
doc() {
  local path="$DIR/$1"
  {
    printf '# fixture\n\n'
    printf '%s\n' "$2"
  } >"$path"
  printf '%s' "$path"
}

gate() { "$ROOT_DIR/scripts/check-docs.sh" "$1" 2>&1; }

CLEAN=$(doc clean.md 'Documents name the symbol; the command prints the number.')
out=$(gate "$CLEAN")
check_eq "a document with no count passes" 0 "$?"
check_eq "and the report is empty" 0 "$(printf '%s' "$out" | grep -c 'COUNT')"

SPACED=$(doc spaced.md 'the suite reports 651 assertions in one run')
out=$(gate "$SPACED")
check_eq "the space form fails" 1 "$?"
check_eq "and is reported on its own line" 1 "$(printf '%s' "$out" | grep -c "COUNT .*$SPACED:3")"

HYPHEN=$(doc hyphen.md 'the paragraph gained a 375-line keymap dump')
out=$(gate "$HYPHEN")
check_eq "the hyphen form fails" 1 "$?"
check_eq "and is reported on its own line" 1 "$(printf '%s' "$out" | grep -c "COUNT .*$HYPHEN:3")"

PLURAL=$(doc plural.md 'a 12-tests change with a 5-component tree')
out=$(gate "$PLURAL")
check_eq "a hyphenated plural noun fails too" 1 "$?"

# A file name is a token that can carry a number, a hyphen and end in a noun,
# and the gate stays usable only because the number there starts no word.
FILENAME=$(doc filename.md "it starts with \`test-94-fresh-wm-per-file.sh\` and \`test-94-lines.sh\`")
out=$(gate "$FILENAME")
check_eq "a number inside a file name is not a count" 0 "$?"
check_eq "and is not reported" 0 "$(printf '%s' "$out" | grep -c 'COUNT')"

RANGE=$(doc range.md 'the change touches 5-10 files')
out=$(gate "$RANGE")
check_eq "a range whose second number is a count fails" 1 "$?"

OTHER=$(doc other.md 'written on 2026-11-04 in xterm-256color')
out=$(gate "$OTHER")
check_eq "a date and a terminal name are not counts" 0 "$?"

finish
