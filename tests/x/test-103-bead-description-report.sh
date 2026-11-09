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
#     tracker with nothing in it;
#   - the repair the failure prints, `--update-baseline`, run while the tree
#     still carries an issue with no description and no entry: it must refuse
#     rather than record the finding as accepted, which is the whole sequence a
#     wipe used to survive.
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

# --- the repair must not accept the finding ----------------------------------
#
# The failure prints `--update-baseline`, and that command used to be the way a
# wipe went green: it recorded every empty description as debt, so the same run
# that named the bead accepted it. The list is the gate's record of empty
# descriptions it accepts, so an id arriving there is the finding itself.
printf '# Nothing known.\n' >"$BASELINE"
bead_description_write_baseline "$BASELINE" "$WIPED" check-bead-descriptions >"$LAST" 2>&1
check_eq "the generator refuses over a wipe" 1 "$?"
check_eq "and names the bead it would have recorded" 1 "$(grep -c 'WOULD RECORD  wm-a2' "$LAST")"
check_eq "and the null description too" 1 "$(grep -c 'WOULD RECORD  wm-a3' "$LAST")"
check_eq "and does not name the intact bead" 0 "$(grep -c 'WOULD RECORD  wm-a1' "$LAST")"
check_eq "and says where the text is" 1 "$(grep -c "bd history <id> --events', under Old:" "$LAST")"
check_eq "and says nothing was written" 1 "$(grep -c '^Nothing was written.$' "$LAST")"
check_eq "and the file is unchanged" '' "$(grep -v '^#' "$BASELINE")"
bead_description_verify "$WIPED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "and the gate is still red afterwards" 1 "$?"

# The one repair the command has: an entry whose description is back, and one
# the tracker no longer carries, are dropped. wm-a2 keeps its entry because it
# still has no description, and nothing arrives, so this is the shape the
# command is meant to handle.
DROPPED="$DIR/dropped.json"
cat >"$DROPPED" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with a description", "description": "why it exists"},
  {"id": "wm-a2", "status": "open", "title": "another", "description": ""}
]
EOF
printf '# Nothing known.\nwm-a1\nwm-a2\nwm-a9\n' >"$BASELINE"
bead_description_write_baseline "$BASELINE" "$DROPPED" check-bead-descriptions >"$LAST" 2>&1
check_eq "the generator drops what the tracker no longer needs" 0 "$?"
check_eq "and keeps the id that is still empty" wm-a2 "$(grep -v '^#' "$BASELINE")"
bead_description_verify "$DROPPED" "$BASELINE" check-bead-descriptions >"$LAST" 2>&1
check_eq "a baseline regenerated from the tracker accepts that tracker" 0 "$?"

# The committed file and this writer have to agree, or a regeneration mixes a
# header revert into the entry change and the reader cannot tell which lines
# the command owns.
check_eq "the generator writes the committed file's header" '' \
  "$(diff <(grep '^#' "$BASELINE") <(grep '^#' "$ROOT_DIR/tests/quality/bead-description-baseline.txt"))"

# --- the gate itself, end to end --------------------------------------------
#
# The wrapper is what the failure tells the reader to run, so it is run rather
# than grepped: a copy of the script and its library, and a stand-in bd that
# answers with a recorded list.
WRAP="$DIR/wrap"
rm -rf "$WRAP"
mkdir -p "$WRAP/scripts/lib" "$WRAP/tests/quality" "$DIR/bin"
cp "$ROOT_DIR/scripts/check-bead-descriptions.sh" "$WRAP/scripts/"
cp "$ROOT_DIR/scripts/lib/bead-description-report.sh" "$WRAP/scripts/lib/"
cat >"$DIR/bin/bd" <<'EOF'
#!/usr/bin/env bash
case "$*" in
  list*) cat "$STUB_ISSUES" ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$DIR/bin/bd"

WRAP_BASELINE="$WRAP/tests/quality/bead-description-baseline.txt"
printf '# Nothing known.\n' >"$WRAP_BASELINE"
STUB_ISSUES="$WIPED" PATH="$DIR/bin:$PATH" "$WRAP/scripts/check-bead-descriptions.sh" >"$LAST" 2>&1
check_eq "the gate fails on the wipe" 1 "$?"
check_eq "and names the bead" 1 "$(grep -c 'NO DESCRIPTION  wm-a2' "$LAST")"
STUB_ISSUES="$WIPED" PATH="$DIR/bin:$PATH" "$WRAP/scripts/check-bead-descriptions.sh" --update-baseline >"$LAST" 2>&1
check_eq "the repair the failure prints refuses the wipe" 1 "$?"
check_eq "and names the id it would have recorded" 1 "$(grep -c 'WOULD RECORD  wm-a2' "$LAST")"
check_eq "and writes no baseline" '' "$(grep -v '^#' "$WRAP_BASELINE")"
check_eq "and does not claim the file was written" 0 "$(grep -c 'wrote .*read it before committing' "$LAST")"

finish
