#!/usr/bin/env bash
# --update-baseline rewrote the baseline from the files it was handed, so a run
# for one staged file deleted every entry it did not measure, and a commit that
# touched only headers failed the gate because clang-tidy reports no function for
# a header. A test source is not measured at all: a test body's number is its
# assertion count, and the gate's remedy would mean deleting assertions. All
# three are pinned here against a stand-in clang-tidy, so the gate is judged
# without the tool, a compile database, or the tree.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/complexity"
rm -rf "$DIR"
mkdir -p "$DIR/bin" "$DIR/scripts" "$DIR/tests/quality"
cp "$ROOT_DIR/scripts/check-complexity.sh" "$DIR/scripts/"
printf -- '-I.\n' >"$DIR/compile_flags.txt"

# The gate asks clang-tidy for two things: its version, and one line per function
# at or above the threshold. The stand-in answers both from a table keyed by the
# file it was asked about, which makes "what was measured" something the test
# controls. empty.c reports nothing on purpose: that is the broken-tool case the
# guard exists for.
cat >"$DIR/bin/clang-tidy" <<'STUB'
#!/usr/bin/env bash
if [ "$1" = "--version" ]; then
  echo "LLVM version 0.0.0-test"
  exit 0
fi
for arg in "$@"; do
  case $arg in
    one.c)
      echo "$arg:1:1: warning: function 'alpha' has cognitive complexity of 20 [a]"
      echo "$arg:2:1: warning: function 'beta' has cognitive complexity of 12 [a]"
      ;;
    two.c) echo "$arg:1:1: warning: function 'gamma' has cognitive complexity of 30 [a]" ;;
    three.c) echo "$arg:1:1: warning: function 'delta' has cognitive complexity of 40 [a]" ;;
    small.c) echo "$arg:1:1: warning: function 'tiny' has cognitive complexity of 3 [a]" ;;
    tests/unit/*.c) echo "$arg:1:1: warning: function 'test_many_assertions' has cognitive complexity of 40 [a]" ;;
    *) : ;;
  esac
done
STUB
chmod +x "$DIR/bin/clang-tidy"

BASELINE="$DIR/tests/quality/complexity-baseline.txt"
seed() {
  cat >"$BASELINE" <<'SEED'
# Cognitive complexity per function, from clang-tidy 0.0.0-test.
# Written by scripts/check-complexity.sh --update-baseline.
#
alpha 20
beta 12
kept 14
SEED
}
gate() { PATH="$DIR/bin:$PATH" "$DIR/scripts/check-complexity.sh" "$@" 2>&1; }

seed
out=$(gate --update-baseline two.c)
check_eq "regenerating for one file succeeds" 0 "$?"
check_eq "and adds the function it measured" 1 "$(grep -c '^gamma 30$' "$BASELINE")"
check_eq "and keeps the entries from files it did not measure" 3 \
  "$(grep -cE '^(alpha 20|beta 12|kept 14)$' "$BASELINE")"

# A drop below the floor removes the entry for the function that was measured,
# and still leaves everything else alone.
out=$(gate --update-baseline small.c)
check_eq "a function measured below the floor gets no entry" 0 "$(grep -c '^tiny' "$BASELINE")"
check_eq "and the rest of the baseline is still there" 4 \
  "$(grep -cE '^(alpha 20|beta 12|gamma 30|kept 14)$' "$BASELINE")"

# The second run must not duplicate or reorder what the first wrote.
before=$(grep -v '^#' "$BASELINE")
gate --update-baseline one.c >/dev/null
check_eq "regenerating for an already measured file changes no entry" "$before" \
  "$(grep -v '^#' "$BASELINE")"

out=$(gate empty.c)
check_eq "a source that reports nothing still fails" 1 "$?"
check_eq "and names the database as the thing to check" 1 \
  "$(echo "$out" | grep -c 'clang-tidy reported nothing')"

out=$(gate only.h)
check_eq "a header alone passes" 0 "$?"
check_eq "and says why there was nothing to measure" 1 \
  "$(echo "$out" | grep -c 'no function body')"

out=$(gate only.h one.c)
check_eq "a header beside a source measures the source" 0 "$?"
check_eq "and the header is not reported as a broken database" 0 \
  "$(echo "$out" | grep -c 'clang-tidy reported nothing')"

# A test source is not measured: clang-tidy charges each ASSERT's if/else as a
# decision, so a test body reads as one decision per assertion, and the split
# this gate asks for does not exist. The functions the stand-in reports for a
# test file are the ones the baseline used to carry by hand.
out=$(gate tests/unit/skip.c)
check_eq "a test source alone passes" 0 "$?"
check_eq "and is not asked for a baseline entry" 0 \
  "$(echo "$out" | grep -c 'no baseline entry')"
check_eq "and says it measured nothing" 1 \
  "$(echo "$out" | grep -c 'nothing for this gate to measure')"
check_eq "and is not reported as a broken database" 0 \
  "$(echo "$out" | grep -c 'clang-tidy reported nothing')"

before=$(grep -v '^#' "$BASELINE")
gate --update-baseline tests/unit/skip.c >/dev/null
check_eq "updating from a test source adds no entry" "$before" \
  "$(grep -v '^#' "$BASELINE")"

out=$(gate tests/unit/skip.c three.c)
check_eq "a test source beside an unbaselined source still fails" 1 "$?"
check_eq "and names the production function" 1 \
  "$(echo "$out" | grep -c 'delta is at 40')"

finish
