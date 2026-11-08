#!/usr/bin/env bash
# --update-baseline rewrote the baseline from the files it was handed, so a run
# for one staged file deleted every entry it did not measure, and a commit that
# touched only headers failed the gate because clang-tidy reports no function for
# a header. A test source is not measured at all: a test body's number is its
# assertion count, and the gate's remedy would mean deleting assertions. A
# regeneration without file arguments reads the build's source list, which is not
# the list a commit stages, and it has to carry the tools as well: a tool was
# measured for the first time by the commit that edited it, so a number that had
# been over the floor all along read as that commit's doing. An entry is keyed by
# the file the function was measured in as well as by its name, because keyed by
# name alone a tool's main inherits whatever number the WM's main recorded. All of
# it is pinned here against a stand-in clang-tidy, so the gate is judged without
# the tool, a compile database, or the tree.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/complexity"
rm -rf "$DIR"
mkdir -p "$DIR/bin" "$DIR/scripts" "$DIR/tests/quality"
cp "$ROOT_DIR/scripts/check-complexity.sh" "$DIR/scripts/"
printf -- '-I.\n' >"$DIR/compile_flags.txt"

# The gate asks clang-tidy for two things: its version, and one line per function
# at or above the threshold. The stand-in answers both from a table keyed by the
# file it was asked about, which makes "what was measured" something the test
# controls. It prints that path made absolute: clang-tidy prints the path it was
# handed, rewritten through the compile database, and a relative echo leaves the
# root-prefix strip untested. empty.c reports nothing on purpose: that is the
# broken-tool case the guard exists for. main.c and the tool both define main,
# which is the pair the file key has to tell apart.
cat >"$DIR/bin/clang-tidy" <<'STUB'
#!/usr/bin/env bash
if [ "$1" = "--version" ]; then
  echo "LLVM version 0.0.0-test"
  exit 0
fi
for arg in "$@"; do
  loc="$(pwd)/$arg"
  case $arg in
    one.c)
      echo "$loc:1:1: warning: function 'alpha' has cognitive complexity of 20 [a]"
      echo "$loc:2:1: warning: function 'beta' has cognitive complexity of 12 [a]"
      ;;
    two.c) echo "$loc:1:1: warning: function 'gamma' has cognitive complexity of 30 [a]" ;;
    three.c) echo "$loc:1:1: warning: function 'delta' has cognitive complexity of 40 [a]" ;;
    small.c) echo "$loc:1:1: warning: function 'tiny' has cognitive complexity of 3 [a]" ;;
    main.c) echo "$loc:1:1: warning: function 'main' has cognitive complexity of 20 [a]" ;;
    tools/other.c) echo "$loc:1:1: warning: function 'main' has cognitive complexity of 20 [a]" ;;
    tools/tool.c) echo "$loc:1:1: warning: function 'main' has cognitive complexity of 33 [a]" ;;
    tests/unit/*.c) echo "$loc:1:1: warning: function 'test_many_assertions' has cognitive complexity of 40 [a]" ;;
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
one.c alpha 20
one.c beta 12
main.c main 20
nine.c kept 14
SEED
}
gate() { PATH="$DIR/bin:$PATH" "$DIR/scripts/check-complexity.sh" "$@" 2>&1; }

seed
out=$(gate --update-baseline two.c)
check_eq "regenerating for one file succeeds" 0 "$?"
check_eq "and adds the function it measured" 1 "$(grep -c '^two.c gamma 30$' "$BASELINE")"
check_eq "and keeps the entries from files it did not measure" 4 \
  "$(grep -cE '^(one.c alpha 20|one.c beta 12|main.c main 20|nine.c kept 14)$' "$BASELINE")"

# A drop below the floor removes the entry for the function that was measured,
# and still leaves everything else alone.
out=$(gate --update-baseline small.c)
check_eq "a function measured below the floor gets no entry" 0 "$(grep -c '^small.c tiny' "$BASELINE")"
check_eq "and the rest of the baseline is still there" 5 \
  "$(grep -cE '^(one.c alpha 20|one.c beta 12|main.c main 20|two.c gamma 30|nine.c kept 14)$' "$BASELINE")"

# The second run must not duplicate or reorder what the first wrote.
before=$(grep -v '^#' "$BASELINE")
gate --update-baseline one.c >/dev/null
check_eq "regenerating for an already measured file changes no entry" "$before" \
  "$(grep -v '^#' "$BASELINE")"

# One name, two files, the same number. The entry main.c records for main must
# not exempt the tool's main: keyed by name alone the two are one entry, and the
# number somebody accepted in one file hides the other file's function.
seed
out=$(gate tools/other.c)
check_eq "a function whose name has an entry in another file still fails" 1 "$?"
check_eq "and the message names the file it was measured in" 1 \
  "$(echo "$out" | grep -c 'tools/other.c: main is at 20')"

out=$(gate --update-baseline tools/other.c)
check_eq "baselining the other file's main succeeds" 0 "$?"
check_eq "and records it under its own file" 1 "$(grep -c '^tools/other.c main 20$' "$BASELINE")"
check_eq "and leaves the first file's number alone" 1 "$(grep -c '^main.c main 20$' "$BASELINE")"
out=$(gate main.c tools/other.c)
check_eq "and both files then pass together" 0 "$?"

# A baseline from before the file was part of the key cannot be merged: the old
# lines carry two fields, and merged as three they would lose their number and
# drop out. That silent wipe is what the refusal is for.
cat >"$BASELINE" <<'SEED'
# Cognitive complexity per function, from clang-tidy 0.0.0-test.
alpha 20
SEED
out=$(gate --update-baseline two.c)
check_eq "a baseline without file keys is refused" 1 "$?"
check_eq "and the entry is named" 1 "$(echo "$out" | grep -c 'has an entry that is not')"
check_eq "and the file it was refused with is unchanged" 1 "$(grep -c '^alpha 20$' "$BASELINE")"

out=$(gate empty.c)
check_eq "a source that reports nothing still fails" 1 "$?"
check_eq "and names the database as the thing to check" 1 \
  "$(echo "$out" | grep -c 'clang-tidy reported nothing')"

seed
out=$(gate only.h)
check_eq "a header alone passes" 0 "$?"
check_eq "and says why there was nothing to measure" 1 \
  "$(echo "$out" | grep -c 'nothing for this gate to measure')"

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

# A regeneration with no file arguments reads the build's source list, which is
# not the list the hook hands the gate. When those two disagreed, the baseline
# lost the entries for files the run did not measure, and a test source in the
# build's list got an entry the gate could never check. The list carries the
# tools, so a whole-tree run is where a tool's number is seen first rather than
# the commit that edits the tool.
cat >"$DIR/Makefile" <<'MAKE'
print-built-src:
	@echo "one.c two.c tests/unit/skip.c tools/tool.c"
MAKE
seed
out=$(gate)
check_eq "a whole-tree run fails on a tool's number with no entry" 1 "$?"
check_eq "and names the tool" 1 \
  "$(echo "$out" | grep -c 'tools/tool.c: main is at 33')"

out=$(gate --update-baseline)
check_eq "regenerating with no file arguments succeeds" 0 "$?"
check_eq "and measures the build's source list" 2 \
  "$(grep -cE '^(one.c alpha 20|two.c gamma 30)$' "$BASELINE")"
check_eq "and records the tool in that list" 1 \
  "$(grep -c '^tools/tool.c main 33$' "$BASELINE")"
check_eq "and gives the test source in it no entry" 0 \
  "$(grep -c '^tests/unit/skip.c test_many_assertions 40$' "$BASELINE")"
check_eq "and keeps the entry for a file it did not measure" 1 \
  "$(grep -c '^nine.c kept 14$' "$BASELINE")"

finish
