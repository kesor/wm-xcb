#!/usr/bin/env bash
#
# check-build-variants.sh - the build variants have to share one tree.
#
# Usage: scripts/check-build-variants.sh
#
# Asks make for a sanitizer build, a plain build and a debug build in the same
# checkout, with no `make clean` in between, and checks the binary that each
# one leaves at the tree root. Both directions are checked, because each one
# fails differently. A plain link that reuses -fsanitize objects fails to link
# on __asan_init. A plain `make` that decides the sanitizer binary is already
# up to date links nothing and leaves the wrong build in ./wm, which no link
# error catches. And `make SANITIZE=1` after a plain build links the plain
# objects into an instrumented binary and exits 0, so the run has no sanitizer
# coverage at all while looking correct. That is why the binary at the root is
# checked for ASan symbols and not only for a zero exit status.
#
# The builds print a screenful of -Wpedantic warnings from the debug variant,
# so their output goes to a log that is printed only when a step fails.
#
# Exit status: 0 when every alternation builds and leaves its own binary, 1
# otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

JOBS="$(nproc)"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

fail() {
  echo "check-build-variants: $*" >&2
  exit 1
}

# grep -c, not grep -q: -q exits at the first match and the SIGPIPE reaches nm,
# which under pipefail can turn a found symbol into a non-zero pipeline status.
instrumented() {
  local count
  count="$(nm "$1" 2>/dev/null | grep -c '__asan_init' || true)"
  [ "${count:-0}" -gt 0 ]
}

run_make() {
  local what="$1"
  shift
  if ! make -j"$JOBS" "$@" >>"$LOG" 2>&1; then
    cat "$LOG" >&2
    fail "$what failed"
  fi
  : >"$LOG"
}

run_make "make clean" clean

run_make "make SANITIZE=1" SANITIZE=1
instrumented wm || fail "make SANITIZE=1 built ./wm without ASan symbols"

run_make "plain make after make SANITIZE=1"
instrumented wm && fail "plain make left the instrumented ./wm in place"

run_make "make SANITIZE=1 after a plain build" SANITIZE=1
instrumented wm ||
  fail "make SANITIZE=1 after a plain build linked plain objects: ./wm has no ASan symbols"

run_make "make DEBUG=1 after a plain build" DEBUG=1
run_make "plain make after make DEBUG=1"

run_make "make test SANITIZE=1" test SANITIZE=1
run_make "make test after make test SANITIZE=1" test

echo "check-build-variants: release, sanitize and debug builds share one tree"
