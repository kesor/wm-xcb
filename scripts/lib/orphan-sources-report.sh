# shellcheck shell=bash
#
# lib/orphan-sources-report.sh - compare tracked C sources against the build.
#
# Sourced by scripts/check-orphan-sources.sh, which reads both lists from git and
# make, and by tests/x/test-105-orphan-sources-report.sh, which feeds this
# recorded lists so every shape is pinned without a broken tree.
#
# A .c file that is tracked but missing from SRC is invisible to two things at
# once. The compiler never builds it, and scripts/check-async.sh scans
# `make -s print-src`, so a blocking call in it is not a finding. The file is
# dead in every way that matters, and no gate looked for that until this one.
#
# tests/ and tools/ are outside the comparison: they have their own targets
# (`make test`, `make tools/xclient`) and are not part of the WM's SRC.

# orphan_sources_verify <tracked-file> <built-file> <gate>
#   0 when every tracked source outside tests/ and tools/ is in the built list,
#   1 when one is not, or when either list is empty: an empty list is a read
#   that failed, not a tree with nothing in it.
orphan_sources_verify() {
  local tracked=$1 built=$2 gate=$3
  local -A is_built=()
  local line
  local checked=0 missing=0

  if [ ! -s "$tracked" ]; then
    echo "$gate: the tracked source list is empty, so git ls-files gave nothing to check"
    return 1
  fi

  if [ ! -s "$built" ]; then
    echo "$gate: the build's source list is empty; check 'make -s print-src'"
    return 1
  fi

  while IFS= read -r line; do
    [ -n "$line" ] || continue
    is_built["$line"]=1
  done <"$built"

  while IFS= read -r line; do
    [ -n "$line" ] || continue
    case "$line" in
      tests/* | tools/*) continue ;;
    esac
    checked=$((checked + 1))
    if [ -n "${is_built[$line]:-}" ]; then
      continue
    fi
    missing=$((missing + 1))
    printf '  NOT BUILT  %s\n' "$line"
  done <"$tracked"

  if [ "$missing" -eq 0 ]; then
    [ "${ORPHAN_SOURCES_VERBOSE:-0}" -eq 1 ] &&
      echo "$gate: $checked tracked source(s), every one in the build"
    return 0
  fi

  echo
  echo "$gate: $missing tracked source file(s) that no target compiles."
  echo "The compiler never sees them, and check-async scans the build's source"
  echo "list, so a blocking call in one is invisible to both. Add the file to"
  echo "SRC in the Makefile, or delete it if it is dead."
  return 1
}
