#!/usr/bin/env bash
# --update-baseline rewrote every floor from one run, so raising one file's floor
# also lowered two the worker had not touched: the suite moves by up to 2.6
# points between runs of the same tree, and a floor that follows the measurement
# down cannot notice the loss that comes after it. The rules pinned here are the
# merge, the floor that holds against a run-to-run drop, --allow-lower for a drop
# that is real, the file arguments that say what changed, and the two cases that
# are not about floors at all: a name that matches nothing, and a file deleted
# from the tree.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/coverage-baseline"
rm -rf "$DIR"
mkdir -p "$DIR/src/components"
LIB="$ROOT_DIR/scripts/lib/coverage-baseline.py"
BASELINE="$DIR/coverage-baseline.txt"
MEASURED="$DIR/measured.tsv"

for f in src/components/monitor-manager.c src/components/tiling.c config-file.c \
  wm-states.c wm-hub.c tests/unit/test-wm-hub.c; do
  mkdir -p "$DIR/$(dirname "$f")"
  : >"$DIR/$f"
done

seed() {
  cat >"$BASELINE" <<'SEED'
# Line-coverage floors for the unit suite, per production source file.
# Deleted files are dropped by the next update.
#
src/components/monitor-manager.c 22.7
wm-states.c 36.7
config-file.c 82.5
src/components/tiling.c 63.9
wm-hub.c 87.9
SEED
}
# name<TAB>percent, the shape check-coverage.sh writes after a run. monitor and
# wm-states are the two the bead measured lower than they were recorded; nothing
# about those files changed.
measured() {
  cat >"$MEASURED" <<'MEASURED'
src/components/monitor-manager.c	21.5
wm-states.c	36.2
config-file.c	87.7
src/components/tiling.c	68.0
wm-hub.c	90.0
tests/unit/test-wm-hub.c	55.0
MEASURED
}
gate() { python3 "$LIB" "$@" --measured "$MEASURED" --baseline "$BASELINE" --root "$DIR" 2>&1; }

seed
measured
out=$(gate update)
check_eq "an update with no file arguments succeeds" 0 "$?"
check_eq "and raises the floor of the file that gained tests" 1 \
  "$(grep -c '^config-file.c 84.7$' "$BASELINE")"
check_eq "and holds the floor a run measured lower" 1 \
  "$(grep -c '^src/components/monitor-manager.c 22.7$' "$BASELINE")"
check_eq "and holds the other one" 1 "$(grep -c '^wm-states.c 36.7$' "$BASELINE")"
check_eq "and holds a floor its measurement did not clear by the margin" 1 \
  "$(grep -c '^wm-hub.c 87.9$' "$BASELINE")"
check_eq "and raises the one that cleared the margin" 1 \
  "$(grep -c '^src/components/tiling.c 65.0$' "$BASELINE")"
check_eq "and gives no test file a floor" 0 "$(grep -c '^tests/' "$BASELINE")"
check_eq "and says it held the two the run measured lower" 2 \
  "$(echo "$out" | grep -c 'held .* --allow-lower')"

# One file named: the entry for it moves and every other line stays as it was.
seed
out=$(gate update --pattern config-file.c)
check_eq "an update for one named file succeeds" 0 "$?"
check_eq "and raises that file's floor" 1 "$(grep -c '^config-file.c 84.7$' "$BASELINE")"
check_eq "and leaves the other entries alone" 4 \
  "$(grep -cE '^(src/components/monitor-manager.c 22.7|wm-states.c 36.7|src/components/tiling.c 63.9|wm-hub.c 87.9)$' "$BASELINE")"
check_eq "and adds no new entry it was not asked about" 5 "$(grep -vc '^#' "$BASELINE")"

# A basename is what a worker types; the baseline records the path.
seed
gate update --pattern monitor-manager.c >/dev/null
check_eq "a basename names the file underneath it" 1 \
  "$(grep -c '^src/components/monitor-manager.c 22.7$' "$BASELINE")"

# A drop that is real - code removed, a test deleted - is the flag's job, and
# without the flag the gate stays red rather than silently accepting the drop.
seed
out=$(gate update --pattern wm-states.c --allow-lower)
check_eq "--allow-lower succeeds" 0 "$?"
check_eq "and writes the floor the run measured, less the margin" 1 \
  "$(grep -c '^wm-states.c 33.2$' "$BASELINE")"

# A name that matches nothing is a typo, and an update that quietly changed
# nothing would read as a floor raised.
seed
before=$(cat "$BASELINE")
out=$(gate update --pattern no-such-file.c)
check_eq "a name that matches nothing fails" 1 "$?"
check_eq "and says the floor is unchanged" 1 "$(echo "$out" | grep -c 'the floor is unchanged')"
check_eq "and the baseline is not touched" "$before" "$(cat "$BASELINE")"

# A file that left the tree drops its entry on a full update, and keeps it when
# the update names one other file: the worker cannot know what else moved.
seed
rm -f "$DIR/wm-hub.c"
out=$(gate update --pattern config-file.c)
check_eq "an entry for a deleted file survives a one-file update" 1 \
  "$(grep -c '^wm-hub.c 87.9$' "$BASELINE")"
out=$(gate update)
check_eq "and a full update drops it" 0 "$(grep -c '^wm-hub.c' "$BASELINE")"
check_eq "and says why" 1 "$(echo "$out" | grep -c 'dropped wm-hub.c: the file is gone')"
: >"$DIR/wm-hub.c"

# A new source gets no floor from the comparison - that is the gate's signal that
# somebody has to decide what covers it - and one from the update.
seed
grep -v '^config-file.c ' "$BASELINE" >"$BASELINE.new" && mv "$BASELINE.new" "$BASELINE"
out=$(gate check)
check_eq "a source with no floor fails the check" 1 "$?"
check_eq "and asks for the floor deliberately" 1 "$(echo "$out" | grep -c 'no floor recorded')"
out=$(gate update --pattern config-file.c)
check_eq "the update adds the missing floor" 1 "$(grep -c '^config-file.c 84.7$' "$BASELINE")"

# The check compares what the run measured and leaves the file alone.
seed
measured
before=$(cat "$BASELINE")
out=$(gate check)
check_eq "a check of a tree that moved below its floors fails" 1 "$?"
check_eq "and names the file and the number" 1 \
  "$(echo "$out" | grep -c 'src/components/monitor-manager.c: 21.5% lines, below its floor of 22.7%')"
check_eq "and names the other one" 1 "$(echo "$out" | grep -c 'wm-states.c: 36.2% lines, below its floor of 36.7%')"
check_eq "and does not rewrite the baseline" "$before" "$(cat "$BASELINE")"

out=$(gate check --pattern config-file.c)
check_eq "a check of one named file passes on it" 0 "$?"
check_eq "and reports the floors it compared" 1 \
  "$(echo "$out" | grep -c 'every file meets its recorded floor')"
check_eq "and counts the test files it did not compare" 1 \
  "$(echo "$out" | grep -c '(1 test file(s) reported, not compared)')"

out=$(gate update --pattern config-file.c --pattern wm-states.c)
check_eq "several named files are one update" 0 "$?"
check_eq "and both are handled" 2 \
  "$(grep -cE '^(config-file.c 84.7|wm-states.c 36.7)$' "$BASELINE")"

finish
