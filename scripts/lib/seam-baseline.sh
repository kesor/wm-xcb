# shellcheck shell=bash
#
# lib/seam-baseline.sh - decide which seam baseline entries a run keeps.
#
# Sourced by tests/x/run.sh, and by tests/x/test-109-seam-baseline-filter.sh,
# which calls the functions with fixture files so the decisions are judged
# without a whole seam run.
#
# A baseline entry is <test file>|<assertion label>, and the entry lines are the
# ones that are neither blank nor a comment.
#
# Both decisions here take the set of test files the run started, because
# `--filter` runs part of the suite: such a run knows the failures of the files
# whose name matched and nothing about the rest. Rewriting the file from what it
# measured deleted every entry for a test it never started, and reporting those
# entries as stale named that rewrite as the cure. So an entry for a file that
# exists and did not run is neither stale nor dropped.
#
# A file that no longer exists is the other case: no run can measure it, so its
# entries are stale in every run and are dropped by an update.

# seam_baseline_entries <baseline> - its entry lines, in file order.
seam_baseline_entries() {
  local baseline=$1
  [ -f "$baseline" ] || return 0
  awk '!/^#/ && NF { print }' "$baseline"
}

# seam_baseline_key_file <key> - the test file part of a baseline entry.
seam_baseline_key_file() {
  printf '%s\n' "${1%%|*}"
}

# seam_baseline_has <file> <line> - 0 when <file> holds <line> as a whole line.
# A missing <file> holds nothing: a run with no filter writes no started-files
# list, and every entry must then survive rather than vanish.
seam_baseline_has() {
  local file=$1 want=$2
  [ -f "$file" ] || return 1
  grep -qxF -- "$want" "$file"
}

# seam_baseline_merge <baseline> <tests-dir> <started-files>
#
# Prints the entry lines for the new baseline. Measured keys arrive on stdin: the
# assertions that failed in this run. An entry whose file is in <started-files>
# is replaced by what the run measured, which is how a fixed assertion stops
# being excused. An entry whose file is in <tests-dir> and is not in
# <started-files> is kept as it was. An entry whose file is in neither is gone
# with the test it named.
seam_baseline_merge() {
  local baseline=$1 tests_dir=$2 started=$3 line key file
  {
    while IFS= read -r line; do
      key=$(seam_baseline_key_file "$line")
      file="$tests_dir/$key"
      if [ ! -e "$file" ] || seam_baseline_has "$started" "$key"; then
        continue
      fi
      printf '%s\n' "$line"
    done < <(seam_baseline_entries "$baseline")
    while IFS= read -r line; do
      if [ -n "$line" ]; then
        printf '%s\n' "$line"
      fi
    done
  } | sort -u
}

# seam_baseline_stale <baseline> <tests-dir> <started-files> <seen-keys>
#
# Prints the baseline entries that excuse nothing any more: the assertion did not
# fail in a file this run started, or the file it names is gone.
seam_baseline_stale() {
  local baseline=$1 tests_dir=$2 started=$3 seen=$4 key
  while IFS= read -r key; do
    if [ ! -e "$tests_dir/$(seam_baseline_key_file "$key")" ]; then
      printf '%s\n' "$key"
      continue
    fi
    seam_baseline_has "$seen" "$key" && continue
    seam_baseline_has "$started" "$(seam_baseline_key_file "$key")" || continue
    printf '%s\n' "$key"
  done < <(seam_baseline_entries "$baseline")
}
