# shellcheck shell=bash
#
# lib/path-report.sh - decide whether a committed path names somebody's home.
#
# Sourced by scripts/check-paths.sh. path_hits runs the search; path_verify
# decides. tests/x/test-117-path-report.sh feeds path_verify recorded hits, and
# points path_hits at a fixture repository, so both answers are pinned without
# a dirty tree.
#
# A home path is one that starts a field. A test builds a fake home to run
# against, and a fixture under /tmp/home read as somebody's home: the gate
# failed the commit, and the author renamed the fixture to get past it. That made
# the gate decide what a test may call its own scratch directory, and the cheap
# answer was to weaken the fixture. The character in front of a home is the
# beginning of the line, or one that cannot be part of a path segment.
# /tmp/home does not start a field; /home/someone does.

# A home directory or a personal source tree, at the start of a field. The
# alternatives are inside the boundary group rather than repeated in front of
# it, so a new one is added in one place.
PATH_PATTERN='(^|[^A-Za-z0-9._-])(/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|[Cc]:\\Users\\|~/src/|~/suckless/)'

# The files that spell the pattern above, or write a home path down to check it,
# so the gate searches past itself: this library holds the pattern, the gate
# names this library, and the test that pins both answers writes the paths it
# feeds. A real home path in any other file is still a finding.
PATH_SELF_FILES=(
  scripts/check-paths.sh
  scripts/lib/path-report.sh
  tests/x/test-117-path-report.sh
)

# path_hits <dest>
#   Write git grep's hits for PATH_PATTERN into <dest>, searching the repository
#   of the current directory. Returns git grep's status: 0 found, 1 none, above
#   1 the search did not run.
#   color.ui is forced off on the command. A config of always paints the
#   file:line: prefix even when stdout is a file, and the exemption compares that
#   prefix as text, so the gate then flags the files it searches past.
#   -a is required. Without it a binary match is the line "Binary file <path>
#   matches", which does not contain the pattern, so the re-check below treats
#   a hit as a clean tree.
path_hits() {
  local dest=$1
  git -c color.ui=false grep -a -nE "$PATH_PATTERN" -- . >"$dest" 2>/dev/null
}

# path_verify <hits-file> <gate>
#   <hits-file> is `git grep -n` output: one `file:line:text` hit per line, and
#   empty when the search found nothing. The pattern is applied again here, so
#   the verdict is this function's and a test can pin it on recorded lines
#   without a checkout to search.
#   0 when no hit names a home directory, 1 when one does.
path_verify() {
  local hits=$1 gate=$2
  local hit file name found=0 self=0

  while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    [[ $hit =~ $PATH_PATTERN ]] || continue
    file=${hit%%:*}
    self=0
    for name in "${PATH_SELF_FILES[@]}"; do
      [ "$file" = "$name" ] && self=1
    done
    [ "$self" -eq 1 ] && continue
    found=$((found + 1))
    printf '  HOME PATH  %s\n' "$hit"
  done <"$hits"

  [ "$found" -eq 0 ] && return 0

  echo
  echo "$gate: $found line(s) name a home directory or a personal source tree."
  echo "Use a path relative to the repository, or a placeholder like <path-to-repo>."
  echo "What counts as a home is the field boundary in scripts/lib/path-report.sh."
  return 1
}
