#!/usr/bin/env bash
#
# check-coverage.sh - what the unit suite actually executes, per file.
#
# Usage: scripts/check-coverage.sh [--update-baseline] [--allow-lower] [--show-zero]
#                                   [file...]
#
# With file arguments only those files are compared, or updated: a path relative
# to the repository root, or a suffix of one, so `tiling.c` names
# src/components/tiling.c. The rest of the baseline is left alone.
#
# A test that passes tells you the path it took works. It says nothing about the
# paths it never took. This builds the unit suite with gcov instrumentation, runs
# it, and prints line and branch coverage per source file, so a file the suite
# never enters - or a function whose error branch nobody has ever taken - shows
# up as a number instead of as confidence.
#
# The baseline in tests/quality/coverage-baseline.txt records a line floor per
# production file. A file that drops below its floor fails the run: a change
# that adds a branch nobody tests is the thing this is for. A file absent from
# the baseline fails too, so a new source file cannot arrive without somebody
# deciding what covers it.
#
# The test files under tests/ are printed and not compared. Their missed lines
# are the guards inside the tests' own helpers - a mkdtemp, strdup or fopen that
# failed, a stand-in the test could not write - and no passing test reaches one
# without breaking its environment. A floor there measures how much defensive
# code the helpers carry, and it fails the branch that adds the tests: adding a
# helper that removes a directory dropped tests/unit/test-launcher.c below a
# floor set when the file was a third of its size.
#
# The floor written for a file is this run's line coverage less MARGIN, because
# the suite is not perfectly deterministic: the launcher tests fork and exec, and
# the child's path is covered or not depending on scheduling - and on whether the
# child finished writing its counters before the test binary exited. One file
# moved 0.9 points between two runs here and 2.6 points on a CI runner. The margin
# is sized for the runner, not for this machine. The tests that made it necessary
# are the launcher's, and making them deterministic would let it go back to one
# point.
#
# --update-baseline merges this run into the floors already recorded rather than
# rewriting the file: the entries for files the run did not measure, or that were
# not named, keep their floors, and so does a file this run measured lower than
# its floor - that is the measurement moving, not a coverage decision, and a
# lowered floor cannot notice the loss that follows. Code removed, or a test
# deleted on purpose, is --allow-lower, and the commit that uses it says why:
# lowering a floor to make a red run green is the same as deleting a test.
#
# Exit status: 0 when every file meets its floor, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

# Absolute: the run happens with the build directory as its working directory.
BASELINE="$ROOT_DIR/tests/quality/coverage-baseline.txt"
MARGIN=3.0
UPDATE=0
ALLOW_LOWER=0
SHOW_ZERO=0
FILES=()
for arg in "$@"; do
  case $arg in
    --update-baseline) UPDATE=1 ;;
    --allow-lower) ALLOW_LOWER=1 ;;
    --show-zero) SHOW_ZERO=1 ;;
    -*)
      echo "check-coverage: unknown argument '$arg'" >&2
      exit 1
      ;;
    *) FILES+=("$arg") ;;
  esac
done

if [ "$ALLOW_LOWER" -eq 1 ] && [ "$UPDATE" -eq 0 ]; then
  echo "check-coverage: --allow-lower only means something with --update-baseline" >&2
  exit 1
fi

if ! command -v gcov >/dev/null 2>&1; then
  echo "check-coverage: gcov is not on PATH. Start the dev shell." >&2
  exit 1
fi

# A cache of compiled objects, not a build output: `make clean` removes build/, and
# two gates run it on purpose (build-variants to prove a clean tree builds, and
# sanitize to rebuild its variant). Keeping the cache under build/ meant those
# gates turned a 0.45s check into a 7s one.
BUILD=.cache/coverage
MEASURED="$ROOT_DIR/$BUILD/measured.tsv"
mkdir -p "$BUILD"
# Incremental: the object files are valid until their source changes, and the
# counters reset by deleting the .gcda. A full rebuild took 7s every time, which
# is the expensive part of a gate that runs on every push.
find "$BUILD" -name '*.gcda' -delete 2>/dev/null || true

# The same sources the suite links, with the same flags plus instrumentation.
SOURCES=$(make -s print-src) || {
  echo "check-coverage: cannot read the source list from make" >&2
  exit 1
}
TEST_SOURCES=$(make -s print-test-src) || {
  echo "check-coverage: cannot read the test source list from make" >&2
  exit 1
}

read -r -a PKG_CFLAGS <<<"$(pkg-config --cflags xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms)"
read -r -a PKG_LIBS <<<"$(pkg-config --libs xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms)"
CFLAGS=(
  -g -O0 --coverage -DWM_HUB_TESTING -D_DEFAULT_SOURCE -DVERSION=\"0.0.1\"
  "${PKG_CFLAGS[@]}"
  -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include
)
LDFLAGS=(--coverage "${PKG_LIBS[@]}" -pthread)

objects=()
for src in $SOURCES; do
  case $src in wm.c|*/wm.c) continue ;; esac
  obj="$BUILD/${src//\//_}.o"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    ${CC:-gcc} "${CFLAGS[@]}" -w -c "$src" -o "$obj" || {
      echo "check-coverage: cannot compile $src" >&2
      exit 1
    }
  fi
  objects+=("$obj")
done
for src in $TEST_SOURCES; do
  obj="$BUILD/tests_${src//\//_}.o"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    ${CC:-gcc} "${CFLAGS[@]}" -w -c "$src" -o "$obj" || {
      echo "check-coverage: cannot compile $src" >&2
      exit 1
    }
  fi
  objects+=("$obj")
done

BIN="$BUILD/test"
relink=0
for obj in "${objects[@]}"; do
  [ "$obj" -nt "$BIN" ] && relink=1
done
if [ "$relink" = 1 ] || [ ! -x "$BIN" ]; then
  ${CC:-gcc} -o "$BIN" "${objects[@]}" "${LDFLAGS[@]}" || {
    echo "check-coverage: cannot link the suite" >&2
    exit 1
  }
fi
"$BIN" >"$BUILD/run.log" 2>&1
if ! grep -qE '^[[:space:]]*Total: [0-9]+ passed, 0 failed' "$BUILD/run.log"; then
  echo "check-coverage: the suite did not pass, so its coverage says nothing" >&2
  tail -20 "$BUILD/run.log" >&2
  exit 1
fi
echo "check-coverage: $(grep -E '^[[:space:]]*Total:' "$BUILD/run.log" | tail -1 | sed 's/^ *//')"

cd "$BUILD" || exit 1
# gcov is run from the build directory, so it takes the objects' names, not the
# paths this script built them at.
names=()
for obj in "${objects[@]}"; do names+=("$(basename "$obj")"); done
gcov -j -b -o . "${names[@]}" >/dev/null 2>gcov.err || true
# gcov writes the JSON gzipped, and the dev shell's python has no zlib module.
gzip -df ./*.gcov.json.gz 2>/dev/null || true

# Stop here when the measurement fails. measured.tsv is the previous run's
# until this step rewrites it, and the comparison below would pass the gate
# on those numbers.
if ! python3 - "$ROOT_DIR" "$SHOW_ZERO" "$UPDATE" "$MEASURED" <<'PY'
import glob, json, os, sys

root, show_zero, update, measured_path = (
    sys.argv[1], sys.argv[2] == "1", sys.argv[3] == "1", sys.argv[4])

files = {}
for path in glob.glob("*.gcov.json"):
    with open(path) as fh:
        data = json.load(fh)
    for f in data.get("files", []):
        name = f.get("file", "")
        if not name.endswith(".c"):
            continue
        # gcov writes the path relative to the compiler's working directory.
        absolute = name if os.path.isabs(name) else os.path.join(root, name)
        rel = os.path.relpath(absolute, root)
        if not os.path.exists(os.path.join(root, rel)):
            continue
        lines = f.get("lines", [])
        counted = [ln for ln in lines if ln.get("count") is not None]
        hit = [ln for ln in counted if ln["count"] > 0]
        line_pct = (100.0 * len(hit) / len(counted)) if counted else 0.0
        # gcov puts branch records on each line, not on the file.
        branches = [b for ln in lines for b in ln.get("branches", [])]
        b_hit = [b for b in branches if b.get("count", 0) > 0]
        branch_pct = (100.0 * len(b_hit) / len(branches)) if branches else 100.0
        # A file can appear twice (once per object); keep the better view.
        prev = files.get(rel)
        if prev is None or line_pct > prev[0]:
            files[rel] = (line_pct, branch_pct, len(counted), len(branches))

if not files:
    print("check-coverage: gcov produced nothing to read", file=sys.stderr)
    sys.exit(1)

rows = sorted(files.items(), key=lambda kv: kv[1][0])
print(f"{'file':<48} {'lines':>6} {'branch':>7}")
for name, (line_pct, branch_pct, n_lines, n_branch) in rows:
    print(f"{name:<48} {line_pct:5.1f}% {branch_pct:6.1f}%")

zero = [n for n, v in files.items() if v[0] == 0.0]
if zero and (show_zero or not update):
    print()
    print("never executed by the unit suite:")
    for name in zero:
        print(f"  {name}")

# The decision on what this run does to the floors is scripts/lib/coverage-baseline.py:
# merging into the recorded entries instead of rewriting them is a rule about the
# baseline, and it is pinned there by tests/x/test-111-coverage-baseline.sh.
with open(measured_path, "w") as fh:
    for name, (line_pct, _b, _l, _br) in rows:
        fh.write(f"{name}\t{line_pct:.4f}\n")
PY
then
  exit 1
fi

args=()
if [ "$UPDATE" -eq 1 ]; then
  args=(update)
  if [ "$ALLOW_LOWER" -eq 1 ]; then args+=(--allow-lower); fi
else
  args=(check)
fi
args+=(--measured "$MEASURED" --baseline "$BASELINE" --root "$ROOT_DIR" --margin "$MARGIN")
for f in "${FILES[@]}"; do args+=(--pattern "$f"); done
python3 "$ROOT_DIR/scripts/lib/coverage-baseline.py" "${args[@]}"
