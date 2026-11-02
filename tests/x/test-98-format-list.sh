#!/usr/bin/env bash
# The commit gate formats the files it is given, and the message it prints tells
# you to run `make format`. While that fixer built its list from `git ls-files`
# alone, a new test file was invisible to it: it printed a count, exited 0, and
# the commit it was preparing was then rejected for that file. This pins the
# list the fixer and the gate share, so narrowing it back to the index fails
# here instead of costing the next person a round trip.
source "$(dirname "$0")/lib.sh"

PROBE="$ROOT_DIR/tests/unit/format-list-probe.c"
PROBE_REL="tests/unit/format-list-probe.c"
cleanup_probe() { rm -f "$PROBE"; }
trap cleanup_probe EXIT

rm -f "$PROBE"
check_eq "the probe name starts as no file at all" 0 "$([ -e "$PROBE" ] && echo 1 || echo 0)"

printf 'int probe(void){return 0;}\n' >"$PROBE"
check_eq "the probe is not in the index yet" 0 \
  "$(git -C "$ROOT_DIR" ls-files --error-unmatch "$PROBE_REL" >/dev/null 2>&1 && echo 1 || echo 0)"

LIST="$TMPDIR_SEAM/format-list.txt"
"$ROOT_DIR/scripts/format-check.sh" --list >"$LIST" 2>&1

check_eq "a tracked source is in the list the gate owns" 1 "$(grep -cx 'wm.c' "$LIST")"
check_eq "a tracked header is in it" 1 "$(grep -cx 'wm-hub.h' "$LIST")"
check_eq "an untracked source is in it, before anything stages it" 1 "$(grep -cx "$PROBE_REL" "$LIST")"
check_eq "the list names a file once" 0 "$(sort "$LIST" | uniq -d | grep -c .)"

cleanup_probe
check_eq "the probe leaves nothing behind" 0 "$([ -e "$PROBE" ] && echo 1 || echo 0)"

finish
