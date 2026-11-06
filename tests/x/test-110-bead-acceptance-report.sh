#!/usr/bin/env bash
# scripts/check-bead-acceptance.sh exists because acceptance criteria can be
# erased with a success message: `bd update <id> --acceptance "$(...)"` accepts
# the empty substitution, prints "Updated issue" and exits 0, and unlike
# --description there is no stdin form for bd to refuse an empty body with. bd
# also omits the key when the field is empty, so a wipe and a bead that never
# had criteria are the same bytes, which is why the check is baseline-relative.
#
# The verdict is in scripts/lib/bead-acceptance-report.sh so this feeds it
# recorded issue lists instead of needing a database. The shapes that matter are
# the ones the command line lets through:
#
#   - the key absent, which is what `bd list --json` writes for an empty field,
#     and a null value, which is what the JSONL export writes;
#   - whitespace only, which `--acceptance '  '` accepts and exits 0 on;
#   - a baselined id missing from the list, so nothing about it was checked;
#   - a list where no issue carries the key at all, which is the field missing
#     from the output and not a bead losing its text;
#   - a file that is not an issue list, which is a broken read.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/bead-acceptance-report.sh
. "$ROOT_DIR/scripts/lib/bead-acceptance-report.sh"

DIR="$TMPDIR_SEAM/bead-acceptance"
rm -rf "$DIR"
mkdir -p "$DIR"

LAST="$DIR/last.txt"
BASELINE="$DIR/baseline.txt"

# Two issues with criteria, written the way the tracker writes them: the text is
# a string and the key is there.
KEPT="$DIR/kept.json"
cat >"$KEPT" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with criteria", "acceptance_criteria": "the window is mapped"},
  {"id": "wm-a2", "status": "in_progress", "title": "another", "acceptance_criteria": "line one\nline two\n"}
]
EOF

# The wipe, in both shapes bd writes: an empty value drops the key from the list
# output, and the export records it as null. wm-a1 is intact, so the gate fails
# on these two issues and not because the whole list lost the field.
WIPED="$DIR/wiped.json"
cat >"$WIPED" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with criteria", "acceptance_criteria": "the window is mapped"},
  {"id": "wm-a2", "status": "in_progress", "title": "the key was dropped"},
  {"id": "wm-a3", "status": "open", "title": "the value is null", "acceptance_criteria": null}
]
EOF

# Whitespace only: accepted by the inline flag where an empty body is not.
SPACES="$DIR/spaces.json"
cat >"$SPACES" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with criteria", "acceptance_criteria": "the window is mapped"},
  {"id": "wm-a4", "status": "open", "title": "erased with a space", "acceptance_criteria": "  \n\t\n"}
]
EOF

printf '# Nothing known.\nwm-a1\nwm-a2\nwm-a3\n' >"$BASELINE"

bead_acceptance_verify "$WIPED" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "an emptied key fails" 1 "$?"
check_eq "and the bead is named" 1 "$(grep -c 'NO ACCEPTANCE  wm-a2 (in_progress) the key was dropped' "$LAST")"
check_eq "a null value fails too" 1 "$(grep -c 'NO ACCEPTANCE  wm-a3 (open) the value is null' "$LAST")"
check_eq "and the intact bead is not reported" 0 "$(grep -c 'NO ACCEPTANCE  wm-a1' "$LAST")"
check_eq "the failure says where the text went" 1 "$(grep -c 'bd history <id> --events' "$LAST")"
check_eq "and says --acceptance is the only write path" 1 "$(grep -c "^  bd update <id> --acceptance" "$LAST")"

# The null case has to fail on its own verdict, not because the recorded list
# was unreadable: a parse error would also exit 1 and name nothing.
NULL_ONLY="$DIR/null-only.json"
cat >"$NULL_ONLY" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with criteria", "acceptance_criteria": "the window is mapped"},
  {"id": "wm-a3", "status": "open", "title": "the value is null", "acceptance_criteria": null}
]
EOF
printf 'wm-a3\n' >"$BASELINE"
bead_acceptance_verify "$NULL_ONLY" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "a null alone fails" 1 "$?"
check_eq "with the bead named" 1 "$(grep -c 'NO ACCEPTANCE  wm-a3' "$LAST")"
check_eq "and not because the file could not be read" 0 "$(grep -c 'nothing was read' "$LAST")"
# The default on the field is what keeps a null from becoming a jq error, and a
# jq error would still exit non-zero while leaving the verdict to luck.
check_eq "and no jq error behind the verdict" 0 "$(grep -c 'jq: error' "$LAST")"
check_eq "and the field is not reported missing from the whole output" 0 \
  "$(grep -c 'missing from the tracker output' "$LAST")"

printf 'wm-a1\nwm-a4\n' >"$BASELINE"
bead_acceptance_verify "$SPACES" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "criteria of whitespace only count as gone" 1 "$?"
check_eq "and the bead is named" 1 "$(grep -c 'NO ACCEPTANCE  wm-a4' "$LAST")"

printf '# Nothing known.\nwm-a1\nwm-a2\n' >"$BASELINE"
bead_acceptance_verify "$KEPT" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "intact criteria pass" 0 "$?"
check_eq "and the pass line counts what it keeps" 1 "$(grep -c '2 with acceptance criteria to keep, all present and intact' "$LAST")"

# A baselined id the list does not carry is not a pass: the gate read nothing
# about it, and the baseline was written because the text is meant to exist.
printf 'wm-a1\nwm-a9\n' >"$BASELINE"
bead_acceptance_verify "$KEPT" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "a baselined id missing from the list fails" 1 "$?"
check_eq "and is named as absent" 1 "$(grep -c 'NOT IN THE TRACKER  wm-a9' "$LAST")"
check_eq "and the failure says nothing about it was checked" 1 "$(grep -c 'so nothing about them was checked' "$LAST")"

# The field gone from every row is a different defect from one bead losing it,
# and the verdict has to say which: otherwise the reader chases a wipe that
# never happened.
NOFIELD="$DIR/nofield.json"
cat >"$NOFIELD" <<'EOF'
[
  {"id": "wm-a1", "status": "open", "title": "a bead with criteria"},
  {"id": "wm-a2", "status": "open", "title": "another"}
]
EOF
printf 'wm-a1\nwm-a2\n' >"$BASELINE"
bead_acceptance_verify "$NOFIELD" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "a list with no acceptance field fails" 1 "$?"
check_eq "and names the field as missing from the output" 1 "$(grep -c 'missing from the tracker output rather than from these beads alone' "$LAST")"

# An issue with criteria that the baseline does not list is not a failure: the
# baseline is not debt, and failing here would make a normal --acceptance write
# stop the build until someone ran the update. It is reported, because otherwise
# the list rots and a wipe of that bead goes unseen.
printf 'wm-a1\n' >"$BASELINE"
bead_acceptance_verify "$KEPT" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "an unlisted issue with criteria passes" 0 "$?"
check_eq "and is reported as not protected" 1 "$(grep -c '1 issue(s) carry acceptance criteria and are not in the baseline' "$LAST")"
# The report tells the reader to run --update-baseline, and a bead created
# mid-cycle makes that the last step of a commit already being made. Naming the
# file is what says so: without it the update reads as a second commit, and the
# bead stays unprotected until someone runs it by hand.
check_eq "and names the baseline file the update writes" 1 \
  "$(grep -c 'tests/quality/bead-acceptance-baseline.txt' "$LAST")"
check_eq "and that is the file the gate reads" 1 \
  "$(grep -c '^BASELINE=.*tests/quality/bead-acceptance-baseline.txt"' "$ROOT_DIR/scripts/check-bead-acceptance.sh")"

# A file that is not an issue list is a broken read. The wrapper skips before it
# gets here, but the verdict is what the gate's meaning rests on, so it refuses
# rather than reading it as a tracker with nothing in it.
NOT_JSON="$DIR/not-json.txt"
printf 'no beads database in this checkout\n' >"$NOT_JSON"
bead_acceptance_verify "$NOT_JSON" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "a file that is not an issue list fails" 1 "$?"
check_eq "and says nothing was read" 1 "$(grep -c 'so nothing was read' "$LAST")"

# An empty baseline protects nothing and says so, rather than reporting a clean
# tracker it never checked.
EMPTY="$DIR/empty.json"
printf '[]\n' >"$EMPTY"
printf '# Nothing known.\n' >"$BASELINE"
bead_acceptance_verify "$EMPTY" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "an empty baseline passes" 0 "$?"
check_eq "and says nothing is protected" 1 "$(grep -c 'no baseline entries, so no acceptance criteria are protected yet' "$LAST")"

# The generator writes the ids the gate then accepts, so --update-baseline
# cannot produce a file the gate still rejects.
bead_acceptance_write_baseline "$BASELINE" "$KEPT"
check_eq "the generator records one id per issue with criteria" 2 "$(grep -vc '^#' "$BASELINE")"
check_eq "and the generator line names the command" 1 \
  "$(grep -c '^# Generated by scripts/check-bead-acceptance.sh --update-baseline.$' "$BASELINE")"
bead_acceptance_verify "$KEPT" "$BASELINE" check-bead-acceptance >"$LAST" 2>&1
check_eq "a baseline written from the list accepts that list" 0 "$?"

# The row the gate writes is `<id>TAB<status>TAB<title>` with tabs and newlines
# in the title flattened, so a title cannot split a row into fields the report
# then reads as the status.
TABBED="$DIR/tabbed.json"
cat >"$TABBED" <<'EOF'
[{"id": "wm-a5", "status": "open", "title": "two\tlines", "acceptance_criteria": "x"}]
EOF
check_eq "a tab in a title stays in one field" 3 \
  "$(bead_acceptance_with_text "$TABBED" | awk -F'\t' '{print NF}')"

finish
