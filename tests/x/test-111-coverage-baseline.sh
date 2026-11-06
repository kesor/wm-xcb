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

# A basename is what a worker types; the baseline records the path. The file
# has to be one this run would raise: a hold leaves the same line whether the
# name matched or the update never saw it.
seed
out=$(gate update --pattern tiling.c)
check_eq "a basename names the file underneath it" 0 "$?"
check_eq "and raises that file" 1 \
  "$(grep -c '^src/components/tiling.c 65.0$' "$BASELINE")"
check_eq "and leaves a file the basename does not name" 1 \
  "$(grep -c '^src/components/monitor-manager.c 22.7$' "$BASELINE")"

seed
out=$(gate update --pattern components/tiling.c)
check_eq "a path suffix names the file" 0 "$?"
check_eq "and raises it" 1 "$(grep -c '^src/components/tiling.c 65.0$' "$BASELINE")"

seed
out=$(gate update --pattern src/components/tiling.c)
check_eq "a path relative to the root names the file" 0 "$?"
check_eq "and raises it too" 1 "$(grep -c '^src/components/tiling.c 65.0$' "$BASELINE")"

# `ling.c` is a suffix of tiling.c and not a path component. Matching on the
# raw suffix would raise the floor; refusing the name must leave it.
seed
out=$(gate update --pattern ling.c)
check_eq "a suffix that is not its own path component fails" 1 "$?"
check_eq "and does not raise the longer name" 1 \
  "$(grep -c '^src/components/tiling.c 63.9$' "$BASELINE")"

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

# The writer replaces the header. The seed still says every update drops a
# deleted file, which a one-file update does not do.
seed
gate update --pattern config-file.c >/dev/null
check_eq "a written baseline does not say every update drops a deleted file" 0 \
  "$(grep -c 'dropped by the next update' "$BASELINE")"
check_eq "and says a full update is what drops one" 1 \
  "$(grep -c 'full update drops' "$BASELINE")"
check_eq "the committed baseline says the same" 1 \
  "$(grep -c 'full update drops' "$ROOT_DIR/tests/quality/coverage-baseline.txt")"
bad=$(grep -l 'src/config-file.c' "$ROOT_DIR/docs/QUALITY.md" \
  "$ROOT_DIR/scripts/check-coverage.sh" "$ROOT_DIR/scripts/lib/coverage-baseline.py" || true)
check_eq "the docs do not name src/config-file.c" "" "$bad"

# A drop of any size is held. The 10.0 measurement against a 22.7 floor is
# not inside the 3.0 margin; a note that says it is tells the reader the tool
# judged the drop to be noise.
seed
cat >"$MEASURED" <<'MEASURED'
src/components/monitor-manager.c	10.0
wm-states.c	36.2
config-file.c	87.7
src/components/tiling.c	68.0
wm-hub.c	90.0
tests/unit/test-wm-hub.c	55.0
MEASURED
out=$(gate update --pattern monitor-manager.c)
check_eq "a drop larger than the margin still updates" 0 "$?"
check_eq "and the floor stays where it was" 1 \
  "$(grep -c '^src/components/monitor-manager.c 22.7$' "$BASELINE")"
check_eq "and the note names the flag" 1 \
  "$(echo "$out" | grep -c 'held src/components/monitor-manager.c at 22.7% (measured 10.0%).*--allow-lower')"
check_eq "and does not call the drop inside the margin" 0 \
  "$(echo "$out" | grep -c 'inside the margin')"

# The comparison is a second process. If the measurement step exits 1 and the
# shell continues, that process reads the previous run's measured.tsv and can
# pass the gate on it. The stand-ins answer make, gcov and the compiler so the
# script reaches that step without a build.
out=$("$ROOT_DIR/scripts/check-coverage.sh" --allow-lower 2>&1)
check_eq "--allow-lower without --update-baseline fails" 1 "$?"
check_eq "and says the flag needs the update" 1 \
  "$(echo "$out" | grep -c 'only means something with --update-baseline')"

SHELL_DIR="$TMPDIR_SEAM/coverage-shell"
rm -rf "$SHELL_DIR"
mkdir -p "$SHELL_DIR/scripts/lib" "$SHELL_DIR/bin" "$SHELL_DIR/src" \
  "$SHELL_DIR/tests/quality" "$SHELL_DIR/.cache/coverage"
cp "$ROOT_DIR/scripts/check-coverage.sh" "$SHELL_DIR/scripts/"
cp "$ROOT_DIR/scripts/lib/coverage-baseline.py" "$SHELL_DIR/scripts/lib/"
: >"$SHELL_DIR/src/foo.c"
: >"$SHELL_DIR/src/bar.c"
SHELL_BASE="$SHELL_DIR/tests/quality/coverage-baseline.txt"
MERGER_LOG="$SHELL_DIR/merger.log"
MEASURED_SRC="$SHELL_DIR/measured-src.tsv"
REAL_PY=$(command -v python3)
cat >"$SHELL_DIR/bin/make" <<'STUB'
#!/bin/sh
exit 0
STUB
cat >"$SHELL_DIR/bin/pkg-config" <<'STUB'
#!/bin/sh
exit 0
STUB
cat >"$SHELL_DIR/bin/gcov" <<'STUB'
#!/bin/sh
exit 0
STUB
cat >"$SHELL_DIR/bin/cc" <<'STUB'
#!/bin/sh
out=""
prev=""
for a in "$@"; do
  if [ "$prev" = "-o" ]; then
    out=$a
  fi
  prev=$a
done
if [ -n "$out" ]; then
  printf '#!/bin/sh\necho "Total: 1 passed, 0 failed"\n' >"$out"
  chmod +x "$out"
fi
exit 0
STUB
# The measurement invocation is `python3 - root show update measured`. The
# stand-in tells that call from the comparison, which is a path, and the
# measured file is the last argument. Paths come from the environment so this
# file does not have to quote them into the stand-in.
cat >"$SHELL_DIR/bin/python3" <<'STUB'
#!/bin/sh
if [ "${1:-}" = "-" ]; then
  if [ "${COVERAGE_MEASURE_FAIL:-}" = 1 ]; then
    echo "check-coverage: gcov produced nothing to read" >&2
    exit 1
  fi
  measured=""
  for a in "$@"; do
    measured=$a
  done
  cp "$COVERAGE_MEASURED_SRC" "$measured"
  exit 0
fi
printf 'merger\n' >> "$COVERAGE_MERGER_LOG"
exec "$COVERAGE_REAL_PYTHON" "$@"
STUB
chmod +x "$SHELL_DIR/bin/make" "$SHELL_DIR/bin/pkg-config" "$SHELL_DIR/bin/gcov" \
  "$SHELL_DIR/bin/cc" "$SHELL_DIR/bin/python3"

old_path=$PATH
export PATH="$SHELL_DIR/bin:$PATH"
export CC="$SHELL_DIR/bin/cc"
export COVERAGE_MEASURED_SRC="$MEASURED_SRC"
export COVERAGE_MERGER_LOG="$MERGER_LOG"
export COVERAGE_REAL_PYTHON="$REAL_PY"

printf 'src/foo.c\t90.0000\nsrc/bar.c\t90.0000\n' >"$MEASURED_SRC"
cat >"$SHELL_BASE" <<'SEED'
src/foo.c 50.0
src/bar.c 50.0
SEED
out=$(PATH="$SHELL_DIR/bin:$old_path" CC="$SHELL_DIR/bin/cc" \
  "$SHELL_DIR/scripts/check-coverage.sh" --update-baseline foo.c 2>&1)
check_eq "a measured update still runs" 0 "$?"
check_eq "and the shell raises only the named file" 1 \
  "$(grep -c '^src/foo.c 87.0$' "$SHELL_BASE")"
check_eq "and the shell leaves the file it was not given" 1 \
  "$(grep -c '^src/bar.c 50.0$' "$SHELL_BASE")"

printf 'src/foo.c\t40.0000\n' >"$MEASURED_SRC"
cat >"$SHELL_BASE" <<'SEED'
src/foo.c 50.0
SEED
out=$(PATH="$SHELL_DIR/bin:$old_path" CC="$SHELL_DIR/bin/cc" \
  "$SHELL_DIR/scripts/check-coverage.sh" --update-baseline --allow-lower foo.c 2>&1)
check_eq "the shell passes --allow-lower through" 0 "$?"
check_eq "and the named floor drops" 1 "$(grep -c '^src/foo.c 37.0$' "$SHELL_BASE")"

# Stale numbers that meet the floor. A fall-through exits 0 and rewrites.
printf 'src/foo.c\t90.0000\n' >"$SHELL_DIR/.cache/coverage/measured.tsv"
cat >"$SHELL_BASE" <<'SEED'
src/foo.c 50.0
SEED
before=$(cat "$SHELL_BASE")
: >"$MERGER_LOG"
out=$(COVERAGE_MEASURE_FAIL=1 PATH="$SHELL_DIR/bin:$old_path" CC="$SHELL_DIR/bin/cc" \
  "$SHELL_DIR/scripts/check-coverage.sh" --update-baseline 2>&1)
check_eq "a failed measurement fails the run" 1 "$?"
check_eq "and does not rewrite the floors" "$before" "$(cat "$SHELL_BASE")"
check_eq "and does not run the comparison" 0 "$(grep -c merger "$MERGER_LOG" || true)"

export PATH="$old_path"
unset CC

finish
