#!/usr/bin/env bash
#
# check-test-src.sh - fail when a test file is not built, so its group never runs.
#
# Usage: scripts/check-test-src.sh [--verbose]
#
# A test group registers itself with a constructor, which only exists in the
# binary if the .c file is compiled into it. A file that is never added to
# TEST_SRC compiles nowhere and its assertions never run, while `make test`
# stays green and the count goes up for other reasons. That is how
# test-tag-manager-component.c sat unbuilt and test-wm-window-list.c duplicated
# two symbols nobody noticed.
#
# Standalone tests are built by their own targets, not by `make test`, so they
# are exempt by name.
#
# Exit status: 0 when every test file is either built or explicitly standalone.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
case "${1:-}" in
  -v | --verbose) VERBOSE=1 ;;
  -h | --help)
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  "") ;;
  *)
    echo "check-test-src: unknown argument '$1'" >&2
    exit 1
    ;;
esac

# Built by their own make targets, not by `make test`.
STANDALONE="test-sm-standalone.c test-wm-hub-standalone.c"

mapfile -t BUILT < <(make -s print-test-src 2>/dev/null | tr ' ' '\n' | grep -E '\.c$')
if [ "${#BUILT[@]}" -eq 0 ]; then
  echo "check-test-src: could not read TEST_SRC from 'make -s print-test-src'" >&2
  exit 1
fi

is_built() {
  local want=$1 f
  for f in "${BUILT[@]}"; do
    [ "$f" = "$want" ] && return 0
  done
  return 1
}

is_standalone() {
  local name
  for name in $STANDALONE; do
    [ "$1" = "$name" ] && return 0
  done
  return 1
}

FOUND=0
CHECKED=0
while IFS= read -r f; do
  [ -n "$f" ] || continue
  grep -q 'TEST_GROUP(' "$f" 2>/dev/null || continue
  CHECKED=$((CHECKED + 1))
  if is_built "$f" || is_standalone "$f"; then
    continue
  fi
  FOUND=$((FOUND + 1))
  printf '  NOT BUILT  %s\n' "$f"
  printf '             declares a TEST_GROUP but is missing from TEST_SRC in the Makefile\n'
done < <(git ls-files 'test-*.c' | sort)

if [ "$FOUND" -eq 0 ]; then
  [ "$VERBOSE" -eq 1 ] && echo "check-test-src: ${CHECKED} test file(s) with a group, all built or standalone"
  exit 0
fi

echo
echo "check-test-src: $FOUND test file(s) whose assertions never run."
exit 1
