# shellcheck shell=bash
#
# lib/path-report.sh - decide whether a committed path names somebody's home.
#
# Sourced by scripts/check-paths.sh, which feeds it git grep's output, and by
# tests/x/test-117-path-report.sh, which feeds it recorded hits so both answers
# are pinned without a dirty tree.
#
# The question is not "does this text contain /home/<name>/" but "does a path
# start there". A test builds a fake home to run against, and a fixture under
# /tmp/home read as somebody's home: the gate failed the commit, and the author
# renamed the fixture to get past it. That made the gate decide what a test may
# call its own scratch directory, and the cheap answer was to weaken the
# fixture. The field boundary below is the distinction the gate wants: a home
# directory starts a field, so the character in front of it is either the
# beginning of the line or one that cannot be part of a path segment. /tmp/home
# does not start a field; /home/someone does.

# A home directory or a personal source tree, at the start of a field. The
# alternatives are inside the boundary group rather than repeated in front of
# it, so a new one is added in one place.
PATH_PATTERN='(^|[^A-Za-z0-9._-])(/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|[Cc]:\\Users\\|~/src/|~/suckless/)'

# The files that have to spell the pattern above and the paths it looks for, so
# the gate searches past itself: this library writes the home paths down, the
# gate names this library, and the test that pins both answers has to write the
# paths it feeds. A real home path in any other file is still a finding.
PATH_SELF_FILES=(
  scripts/check-paths.sh
  scripts/lib/path-report.sh
  tests/x/test-117-path-report.sh
)

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
