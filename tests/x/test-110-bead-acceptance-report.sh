#!/usr/bin/env bash
# scripts/check-bead-acceptance.sh exists because acceptance criteria can be
# erased with a success message: `bd update <id> --acceptance "$(...)"` accepts
# the empty substitution, prints "Updated issue" and exits 0, and unlike
# --description there is no stdin form for bd to refuse an empty body with. bd
# also omits the key when the field is empty, so a wipe and a bead that never
# had criteria are the same bytes, which is why the check is baseline-relative.
#
# The two shapes need two baselines and two questions, and this pins both: does
# a bead that had criteria still have them, and does a live bead have any. The
# second is the one a wiped field hides behind, because bd writes the same
# output for both.
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
#   - a live issue with no criteria that no baseline records, which is the bead
#     a worker planned from its description;
#   - a closed issue with none, which is history rather than work;
#   - a recorded issue that has gained criteria, whose text nothing protects yet,
#     and a recorded issue that closed or left the tracker, which protects
#     nothing;
#   - a protected issue that lost its text while --update-baseline runs, which
#     must refuse rather than move the bead into the list that accepts it;
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

# --- the other half: a bead that never had criteria -------------------------
#
# `bd create` is not in this tree and cannot be made to refuse the empty field,
# so the gate is where the gap becomes visible: the commit after the bead is
# created. bd writes no key for the empty field, so the wipe check above reports
# these beads only when the list loses the field everywhere; this list is the
# shape where one bead alone has none.
MISSING="$DIR/missing-baseline.txt"
LIVE="$DIR/live.json"
cat >"$LIVE" <<'EOF'
[
  {"id": "wm-b1", "status": "open", "title": "has criteria", "acceptance_criteria": "the window is mapped"},
  {"id": "wm-b2", "status": "open", "title": "never written"},
  {"id": "wm-b3", "status": "in_progress", "title": "the value is null", "acceptance_criteria": null},
  {"id": "wm-b4", "status": "blocked", "title": "erased with a space", "acceptance_criteria": "   "},
  {"id": "wm-b5", "status": "closed", "title": "closed with none"},
  {"id": "wm-b6", "status": "open", "title": "recorded as having none"}
]
EOF

# wm-b6 is recorded and wm-b5 is closed: the first is debt the gate accepts, the
# second is not work, so neither fails the run - wm-b5 is reported as a stale
# entry instead, because the generator only records live beads. wm-b2, wm-b3 and
# wm-b4 are the three ways a live bead ends up with no text.
printf '# Recorded.\nwm-b5\nwm-b6\n' >"$MISSING"
bead_acceptance_verify_missing "$LIVE" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a live bead with no criteria fails" 1 "$?"
check_eq "and the bead with no key is named" 1 "$(grep -c 'NO CRITERIA  wm-b2 (open) never written' "$LAST")"
check_eq "a null value is named too" 1 "$(grep -c 'NO CRITERIA  wm-b3 (in_progress)' "$LAST")"
check_eq "and whitespace only is named" 1 "$(grep -c 'NO CRITERIA  wm-b4 (blocked)' "$LAST")"
check_eq "a recorded bead is not reported" 0 "$(grep -c 'NO CRITERIA  wm-b6' "$LAST")"
check_eq "a closed bead with none is not reported" 0 "$(grep -c 'NO CRITERIA  wm-b5' "$LAST")"
check_eq "and the bead with criteria is not reported" 0 "$(grep -c 'NO CRITERIA  wm-b1' "$LAST")"
check_eq "the failure names the write path" 1 "$(grep -c "bd update <id> --acceptance 'what done looks like'" "$LAST")"
check_eq "and the update that records the debt" 1 \
  "$(grep -c '^  scripts/check-bead-acceptance.sh --update-baseline$' "$LAST")"
check_eq "and the file that update writes" 1 "$(grep -c "$MISSING is a tracked file" "$LAST")"
check_eq "a failing run counts the new beads" 1 "$(grep -c '3 live issue(s) carry no acceptance criteria, and no baseline records them' "$LAST")"

# Every live bead without text recorded: the gate passes, and the pass line
# counts the debt so a reader can watch it shrink.
printf '# Recorded.\nwm-b2\nwm-b3\nwm-b4\nwm-b6\n' >"$MISSING"
bead_acceptance_verify_missing "$LIVE" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a recorded live bead passes" 0 "$?"
check_eq "and the pass line counts them" 1 "$(grep -c '4 live issue(s) have no acceptance criteria, all recorded in' "$LAST")"

# An entry whose bead gained criteria is not harmless: the protected baseline
# does not carry that text, so until the update moves the id across, a wipe of it
# would read as recorded debt and pass. The gate fails and says which command
# moves it.
printf 'wm-b1\nwm-b2\nwm-b3\nwm-b4\nwm-b6\n' >"$MISSING"
bead_acceptance_verify_missing "$LIVE" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a recorded bead that gained criteria fails" 1 "$?"
check_eq "and is named as unprotected" 1 "$(grep -c 'CRITERIA NOW  wm-b1 (open) has criteria' "$LAST")"
check_eq "and the failure says the update moves it" 1 "$(grep -c 'update moves them into the protected baseline' "$LAST")"
check_eq "and that both baselines are staged" 1 "$(grep -c 'Stage both tracked baselines' "$LAST")"
check_eq "and the still-empty recorded bead is not a failure" 0 "$(grep -c 'NO CRITERIA  wm-b6' "$LAST")"

# An entry that protects nothing - the bead closed, or left the tracker - is
# dropped by the update rather than failed: the stale hint names the file.
printf 'wm-b5\nwm-b9\nwm-b2\nwm-b3\nwm-b4\nwm-b6\n' >"$MISSING"
bead_acceptance_verify_missing "$LIVE" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a closed entry and a gone entry pass" 0 "$?"
check_eq "and are reported as stale" 1 "$(grep -c '2 recorded issue(s) closed or left the tracker' "$LAST")"
check_eq "and names the file the update drops them from" 1 "$(grep -c -- "--update-baseline drops them from $MISSING" "$LAST")"

# A list of closed beads only is a clean answer, not a silent one.
CLOSED_ONLY="$DIR/closed-only.json"
cat >"$CLOSED_ONLY" <<'EOF'
[{"id": "wm-b5", "status": "closed", "title": "closed with none"}]
EOF
bead_acceptance_verify_missing "$CLOSED_ONLY" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a closed bead with none passes" 0 "$?"
check_eq "and the run says every live one has criteria" 1 "$(grep -c 'every live one carrying acceptance criteria' "$LAST")"

bead_acceptance_verify_missing "$NOT_JSON" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "the missing half refuses a file that is not an issue list" 1 "$?"
check_eq "and says nothing was read" 1 "$(grep -c 'so nothing was read' "$LAST")"

# The generator writes the ids the gate then accepts, so --update-baseline
# cannot produce a list the same run rejects.
bead_acceptance_write_missing_baseline "$MISSING" "$LIVE"
check_eq "the generator records one id per live issue with no criteria" 4 "$(grep -vc '^#' "$MISSING")"
check_eq "and writes the recorded ids in order" 'wm-b2 wm-b3 wm-b4 wm-b6' "$(grep -v '^#' "$MISSING" | tr '\n' ' ' | sed 's/ $//')"
check_eq "and the generator line names the command" 1 \
  "$(grep -c '^# Generated by scripts/check-bead-acceptance.sh --update-baseline.$' "$MISSING")"
bead_acceptance_verify_missing "$LIVE" "$MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a list written from the tracker accepts that tracker" 0 "$?"

# The protected generator must not be a way out of a wipe. An id it protects that
# still exists and carries no text is the change the gate exists to catch, and
# regenerating over it would move the bead into the missing list and turn the
# gate green. An id the tracker no longer carries is dropped: that is the one
# repair the command has to be able to make.
PROTECTED="$DIR/protected-baseline.txt"
printf '# Protected.\nwm-b1\nwm-b2\nwm-b9\n' >"$PROTECTED"
bead_acceptance_write_baseline "$PROTECTED" "$LIVE" check-bead-acceptance >"$LAST" 2>&1
check_eq "the generator refuses over a wipe" 1 "$?"
check_eq "and names the id it would have dropped" 1 "$(grep -c 'WOULD DROP  wm-b2' "$LAST")"
check_eq "and says the text is in the history" 1 "$(grep -c "bd history <id> --events', under Old:" "$LAST")"
check_eq "and says nothing was written" 1 "$(grep -c '^Nothing was written.$' "$LAST")"
check_eq "and the file is unchanged" 'wm-b1 wm-b2 wm-b9' "$(grep -v '^#' "$PROTECTED" | tr '\n' ' ' | sed 's/ $//')"

printf '# Protected.\nwm-a1\nwm-a9\nwm-a2\n' >"$PROTECTED"
bead_acceptance_write_baseline "$PROTECTED" "$KEPT" check-bead-acceptance >"$LAST" 2>&1
check_eq "an id the tracker no longer carries is dropped" 0 "$?"
check_eq "and the surviving ids are the ones with text" 'wm-a1 wm-a2' "$(grep -v '^#' "$PROTECTED" | tr '\n' ' ' | sed 's/ $//')"

# --- the two shapes the review found, end to end ----------------------------
#
# A recorded bead that gains criteria must not be able to lose them without the
# gate failing, and the update is what makes that true: after it runs, the id is
# in the protected list, so a wipe is named there.
SEQ="$DIR/sequence"
mkdir -p "$SEQ"
SEQ_PROTECTED="$SEQ/protected.txt"
SEQ_MISSING="$SEQ/missing.txt"
printf '# Protected.\nwm-c1\n' >"$SEQ_PROTECTED"
printf '# Recorded.\nwm-c2\n' >"$SEQ_MISSING"
GAINED="$DIR/gained.json"
cat >"$GAINED" <<'EOF'
[
  {"id": "wm-c1", "status": "open", "title": "protected", "acceptance_criteria": "tiled"},
  {"id": "wm-c2", "status": "open", "title": "was recorded as having none", "acceptance_criteria": "tiled too"}
]
EOF
bead_acceptance_verify_missing "$GAINED" "$SEQ_MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "a gained criterion fails before the update" 1 "$?"
bead_acceptance_write_baseline "$SEQ_PROTECTED" "$GAINED" check-bead-acceptance >/dev/null 2>&1
bead_acceptance_write_missing_baseline "$SEQ_MISSING" "$GAINED"
check_eq "the update moves the id into the protected list" 'wm-c1 wm-c2' \
  "$(grep -v '^#' "$SEQ_PROTECTED" | tr '\n' ' ' | sed 's/ $//')"
check_eq "and out of the missing one" '' "$(grep -v '^#' "$SEQ_MISSING")"
bead_acceptance_verify_missing "$GAINED" "$SEQ_MISSING" check-bead-acceptance >"$LAST" 2>&1
check_eq "and the gate is green afterwards" 0 "$?"

WIPED_BACK="$DIR/wiped-back.json"
cat >"$WIPED_BACK" <<'EOF'
[
  {"id": "wm-c1", "status": "open", "title": "protected", "acceptance_criteria": "tiled"},
  {"id": "wm-c2", "status": "open", "title": "was recorded as having none"}
]
EOF
bead_acceptance_verify "$WIPED_BACK" "$SEQ_PROTECTED" check-bead-acceptance >"$LAST" 2>&1
check_eq "the gained criterion is now protected from a wipe" 1 "$?"
check_eq "and the wipe is named" 1 "$(grep -c 'NO ACCEPTANCE  wm-c2 (open) was recorded as having none' "$LAST")"

# --- the gate itself, end to end --------------------------------------------
#
# The wrapper is what decides that one run carries both verdicts, so it is run
# rather than grepped: a first half that fails must not stop the second naming
# the bead it found. It reads the tracker, so it is run against a copy of the
# two files and a stand-in bd that answers with a recorded list.
WRAP="$DIR/wrap"
rm -rf "$WRAP"
mkdir -p "$WRAP/scripts/lib" "$WRAP/tests/quality" "$DIR/bin"
cp "$ROOT_DIR/scripts/check-bead-acceptance.sh" "$WRAP/scripts/"
cp "$ROOT_DIR/scripts/lib/bead-acceptance-report.sh" "$WRAP/scripts/lib/"
cat >"$DIR/bin/bd" <<'EOF'
#!/usr/bin/env bash
case "$*" in
  list*) cat "$STUB_ISSUES" ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$DIR/bin/bd"

# wm-w1 is protected and gone from the tracker, so the first half fails; wm-w2 is
# live with no criteria and in no baseline, so the second fails too.
WRAP_ISSUES="$DIR/wrap-issues.json"
cat >"$WRAP_ISSUES" <<'EOF'
[
  {"id": "wm-w2", "status": "open", "title": "created with none"},
  {"id": "wm-w3", "status": "open", "title": "protected", "acceptance_criteria": "tiled"}
]
EOF
printf '# Protected.\nwm-w1\nwm-w3\n' >"$WRAP/tests/quality/bead-acceptance-baseline.txt"
printf '# Recorded.\n' >"$WRAP/tests/quality/bead-acceptance-missing-baseline.txt"
STUB_ISSUES="$WRAP_ISSUES" PATH="$DIR/bin:$PATH" "$WRAP/scripts/check-bead-acceptance.sh" >"$LAST" 2>&1
check_eq "one run fails on the wiped and the new bead" 1 "$?"
check_eq "the wiped bead is named" 1 "$(grep -c 'NOT IN THE TRACKER  wm-w1' "$LAST")"
check_eq "and the new bead is named in the same run" 1 "$(grep -c 'NO CRITERIA  wm-w2 (open)' "$LAST")"

# The same wrapper, with the update: a protected bead that still exists and lost
# its text stops the run, and neither tracked file is rewritten.
WRAP_WIPED="$DIR/wrap-wiped.json"
cat >"$WRAP_WIPED" <<'EOF'
[
  {"id": "wm-w3", "status": "open", "title": "protected"},
  {"id": "wm-w4", "status": "open", "title": "new bead with none"}
]
EOF
printf '# Protected.\nwm-w3\n' >"$WRAP/tests/quality/bead-acceptance-baseline.txt"
STUB_ISSUES="$WRAP_WIPED" PATH="$DIR/bin:$PATH" "$WRAP/scripts/check-bead-acceptance.sh" --update-baseline >"$LAST" 2>&1
check_eq "the update refuses over a wipe" 1 "$?"
check_eq "and names the id it would have dropped" 1 "$(grep -c 'WOULD DROP  wm-w3' "$LAST")"
check_eq "and leaves the protected list as it was" 'wm-w3' "$(grep -v '^#' "$WRAP/tests/quality/bead-acceptance-baseline.txt")"
check_eq "and writes no missing list" '' "$(grep -v '^#' "$WRAP/tests/quality/bead-acceptance-missing-baseline.txt")"

finish
