#!/usr/bin/env bash
#
# check-coverage.sh - what the unit suite actually executes, per file.
#
# Usage: scripts/check-coverage.sh [--update-baseline] [--show-zero]
#
# A test that passes tells you the path it took works. It says nothing about the
# paths it never took. This builds the unit suite with gcov instrumentation, runs
# it, and prints line and branch coverage per source file, so a file the suite
# never enters - or a function whose error branch nobody has ever taken - shows
# up as a number instead of as confidence.
#
# The baseline in tests/quality/coverage-baseline.txt records a line floor per
# file. A file that drops below its floor fails the run: a change that adds a
# branch nobody tests is the thing this is for. A file absent from the baseline
# fails too, so a new source file cannot arrive without somebody deciding what
# covers it.
#
# The floor written for a file is this run's line coverage less MARGIN, because
# the suite is not perfectly deterministic: the launcher tests fork and exec, and
# the child's path is covered or not depending on scheduling. One file moved 0.9
# points between two runs of the same tree. A margin keeps a real drop visible
# and scheduling noise out of the gate.
#
# --update-baseline rewrites the floors from this run. Doing that to make a red
# run green is the same as deleting a test: say in the commit why the drop is
# acceptable.
#
# Exit status: 0 when every file meets its floor, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BASELINE="tests/quality/coverage-baseline.txt"
MARGIN=1.5
UPDATE=0
SHOW_ZERO=0
for arg in "$@"; do
  case $arg in
    --update-baseline) UPDATE=1 ;;
    --show-zero) SHOW_ZERO=1 ;;
    *)
      echo "check-coverage: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

if ! command -v gcov >/dev/null 2>&1; then
  echo "check-coverage: gcov is not on PATH. Start the dev shell." >&2
  exit 1
fi

BUILD=build/coverage
rm -rf "$BUILD"
mkdir -p "$BUILD"

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
  ${CC:-gcc} "${CFLAGS[@]}" -w -c "$src" -o "$obj" || {
    echo "check-coverage: cannot compile $src" >&2
    exit 1
  }
  objects+=("$obj")
done
for src in $TEST_SOURCES; do
  obj="$BUILD/tests_${src//\//_}.o"
  ${CC:-gcc} "${CFLAGS[@]}" -w -c "$src" -o "$obj" || {
    echo "check-coverage: cannot compile $src" >&2
    exit 1
  }
  objects+=("$obj")
done

BIN="$BUILD/test"
${CC:-gcc} -o "$BIN" "${objects[@]}" "${LDFLAGS[@]}" || {
  echo "check-coverage: cannot link the suite" >&2
  exit 1
}
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

COVERAGE_MARGIN=$MARGIN python3 - "$ROOT_DIR" "$UPDATE" "$SHOW_ZERO" "$BASELINE" <<'PY'
import glob, json, os, sys

root, update, show_zero = sys.argv[1], sys.argv[2] == "1", sys.argv[3] == "1"
baseline_path = os.path.join(root, sys.argv[4])

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

baseline = {}
if os.path.exists(baseline_path):
    for line in open(baseline_path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            baseline[parts[0]] = float(parts[1])

if update:
    with open(baseline_path, "w") as fh:
        fh.write("# Line-coverage floors for the unit suite, per source file.\n")
        fh.write("# Written by scripts/check-coverage.sh --update-baseline. A file that\n")
        fh.write("# drops below its floor fails the gate; a new file must be added here\n")
        fh.write("# deliberately, which is the moment somebody decides what covers it.\n")
        fh.write("# Deleted files are dropped by the next update.\n#\n")
        for name, (line_pct, _b, _l, _br) in rows:
            floor = max(0.0, line_pct - float(os.environ.get("COVERAGE_MARGIN", "1.5")))
            fh.write(f"{name} {floor:.1f}\n")
    print(f"\ncheck-coverage: wrote {len(rows)} floor(s) to {os.path.relpath(baseline_path, root)}")
    sys.exit(0)

problems = []
for name, (line_pct, _b, _l, _br) in files.items():
    floor = baseline.get(name)
    if floor is None:
        problems.append(f"{name}: no floor recorded ({line_pct:.1f}% lines); add it to the baseline deliberately")
    elif line_pct + 0.05 < floor:
        problems.append(f"{name}: {line_pct:.1f}% lines, below its floor of {floor:.1f}%")

if problems:
    print()
    for p in problems:
        print(f"check-coverage: {p}", file=sys.stderr)
    sys.exit(1)
print("\ncheck-coverage: every file meets its recorded floor")
PY
