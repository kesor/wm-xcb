#!/usr/bin/env bash
# scripts/check-bead-descriptions.sh exists because a description can be erased
# with a success message: `bd update <id> --description "$(...)"` accepted the
# empty substitution, printed "Updated issue" and exited 0, and the bead's whole
# description was gone. The verdict is in scripts/lib/bead-description-report.sh
# so this feeds it recorded issue lists instead of needing a database, and the
# shapes that matter here are the ones the command line lets through:
#
#   - a description that is an empty string, and one bd writes as null;
#   - a description of whitespace only, which `bd update --stdin` accepts where
#     it refuses an empty body;
#   - a file that is not an issue list at all, which is a broken read and not a
#     tracker with nothing in it.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/bead-description-report.sh
. "$ROOT_DIR/scripts/lib/bead-description-report.sh"

DIR="$TMPDIR_SEAM/bead-description"
rm -rf "$DIR"
mkdir -p "$DIR"

LAST="$DIR/last.txt"
BASELINE="$DIR/baseline.txt"

# Three issues, written the way `bd list --json` writes them: the empty
# description is null on the wire, not an empty string.
DESCRIBED="$DIR/described.json"
cat >"$DESCRIBED" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with a description", "description": "why it exists"},
  {"id": "wm-a2", "status": "in_progress", "title": "another", "description": "line one\nline two\n"}
]
EOF

# The wipe: the flag form with an empty value, which exits 0 and writes this.
WIPED="$DIR/wiped.json"
cat >"$WIPED" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with a description", "description": "why it exists"},
  {"id": "wm-a2", "status": "in_progress", "title": "another", "description": ""},
  {"id": "wm-a3", "status": "open", "title": "the one that lost it", "description": null}
]
EOF

# Whitespace only: what `bd update --stdin` accepts where an empty body is
# refused, so a gate that compares the raw string misses it.
SPACES="$DIR/spaces.json"
cat >"$SPACES" <<'EOF'
[
  {"id": "wm-a4", "status": "open", "title": "erased with a space", "description": "  \n\t\n"}
]
EOF

printf '# Nothing known.\n' >"$BASELINE"

bead_description_verify "$DESCRIBED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "every issue with a description passes" 0 "$?"
check_eq "and the pass line says how many it read" 1 "$(grep -c '2 issue(s), every one with a description' "$LAST")"

bead_description_verify "$WIPED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "an empty description fails" 1 "$?"
check_eq "the bead that lost its description is named" 1 "$(grep -c 'NO DESCRIPTION  wm-a2 (in_progress) another' "$LAST")"
check_eq "a null description fails too" 1 "$(grep -c 'NO DESCRIPTION  wm-a3 (open) the one that lost it' "$LAST")"
check_eq "the failure says where the text went" 1 "$(grep -c 'bd history <id> --events' "$LAST")"
check_eq "the failure names the safe write path" 1 "$(grep -c "bd update <id> --stdin" "$LAST")"

bead_description_verify "$SPACES" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "a description of whitespace only counts as gone" 1 "$?"
check_eq "and the bead is named" 1 "$(grep -c 'NO DESCRIPTION  wm-a4' "$LAST")"

# The baseline is debt, so a listed issue passes and is not reported as new.
printf '# Nothing known.\nwm-a2\nwm-a3\n' >"$BASELINE"
bead_description_verify "$WIPED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "a baselined issue does not fail the gate" 0 "$?"
check_eq "the pass line counts the debt" 1 "$(grep -c '2 without a description, all in the baseline' "$LAST")"

# A baseline entry whose issue has a description again is reported and does not
# fail: the list is meant to shrink, and improving the tree is not punished.
bead_description_verify "$DESCRIBED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "a stale baseline entry passes" 0 "$?"
check_eq "and is reported as stale" 1 "$(grep -c 'stale baseline entrie(s)' "$LAST")"

# A file that is not an issue list is a broken read. The wrapper skips before it
# gets here, but the verdict is what the gate's meaning rests on, so it refuses
# rather than reading it as an empty tracker.
NOT_JSON="$DIR/not-json.txt"
printf 'no beads database in this checkout\n' >"$NOT_JSON"
bead_description_verify "$NOT_JSON" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "a file that is not an issue list fails" 1 "$?"
check_eq "and says nothing was read" 1 "$(grep -c 'so nothing was read' "$LAST")"

# An issue list with nothing in it has no description to lose, and the line says
# so: a gate that answers "clean" to an empty read is the shape this repo has
# been bitten by before.
EMPTY="$DIR/empty.json"
printf '[]\n' >"$EMPTY"
bead_description_verify "$EMPTY" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "an empty tracker passes" 0 "$?"
check_eq "and says there was nothing to check" 1 "$(grep -c 'there is no description to lose' "$LAST")"

# The generator writes the ids the gate then accepts, so --update-baseline
# cannot produce a file the gate still rejects.
bead_description_write_baseline "$BASELINE" "$WIPED"
check_eq "the generator records one id per empty description" 2 "$(grep -vc '^#' "$BASELINE")"
check_eq "and the generator line names the command" 1 \
  "$(grep -c '^# Generated by scripts/check-bead-descriptions.sh --update-baseline.$' "$BASELINE")"
bead_description_verify "$WIPED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "a baseline written from the tracker accepts that tracker" 0 "$?"

finish
