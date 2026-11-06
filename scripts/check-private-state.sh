#!/usr/bin/env bash
#
# check-private-state.sh - the values the single-writer rule protects are out of
# a component's reach, and the compiler is what keeps them there.
#
# docs/PHILOSOPHY.md 2.2 says a tracked value changes only through
# sm_raw_write(), sm_transition(), or the client_* accessors. That was advice
# while the struct definitions sat in public headers: a component could assign
# the field and compile, and the WM and the X server then disagreed days later
# with no trace back to the write.
#
# The fix is that a component's translation unit never sees the field. The only
# thing that keeps it that way is compiling the forbidden assignment and
# requiring the compiler to refuse it, which is what this gate does for every
# fixture under tests/compile-fail/:
#
#   with -DPRIVATE_STATE_CONTROL   must compile: the accessor it names exists
#   without it                     must not compile: the field is unreachable
#
# The control build is what tells a real refusal from a fixture that fails
# because an include path moved.
#
# Exit status: 0 when every fixture behaves, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

CC="${CC:-cc}"
PKGLIST=(xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms)

if ! command -v "$CC" >/dev/null 2>&1; then
  echo "check-private-state: $CC is not on PATH" >&2
  exit 1
fi
if ! pkg-config --exists "${PKGLIST[@]}"; then
  echo "check-private-state: pkg-config cannot find ${PKGLIST[*]}" >&2
  exit 1
fi

read -r -a PKG_CFLAGS <<<"$(pkg-config --cflags "${PKGLIST[@]}")"
FLAGS=(
  "${PKG_CFLAGS[@]}"
  -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include
  -DWM_HUB_TESTING -D_DEFAULT_SOURCE
)

shopt -s nullglob
FIXTURES=(tests/compile-fail/*.c)
if [ "${#FIXTURES[@]}" -eq 0 ]; then
  echo "check-private-state: no fixtures under tests/compile-fail/" >&2
  exit 1
fi

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

FAILED=0
for fixture in "${FIXTURES[@]}"; do
  if ! "$CC" -fsyntax-only "${FLAGS[@]}" -DPRIVATE_STATE_CONTROL "$fixture" >"$LOG" 2>&1; then
    echo "check-private-state: $fixture does not compile even through the accessor."
    echo "  The control build proves the fixture and its includes are sound, so this"
    echo "  is a broken fixture or a missing accessor, not a refusal:"
    sed 's/^/    /' "$LOG"
    FAILED=1
    continue
  fi

  if "$CC" -fsyntax-only "${FLAGS[@]}" "$fixture" >"$LOG" 2>&1; then
    echo "check-private-state: $fixture compiled with a field a component must not write."
    echo "  A tracked value is reachable from a component's translation unit again."
    echo "  Move the definition out of the public header and expose an accessor."
    FAILED=1
  fi
done

if [ "$FAILED" -ne 0 ]; then
  exit 1
fi

echo "check-private-state: ${#FIXTURES[@]} fixture(s) refused, their control builds pass"
