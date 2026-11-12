#!/usr/bin/env bash
#
# check-compiler-refusals.sh - the rules whose only check is that the compiler
# refuses the wrong version of the code.
#
# Two rules live here. Both are the compiler's to keep because no unit test can
# see them:
#
#   docs/PHILOSOPHY.md 2.2 - a tracked value changes only through
#   sm_raw_write(), sm_transition(), or the client_* accessors. That was advice
#   while the struct definitions sat in public headers: a component could assign
#   the field and compile, and the WM and the X server then disagreed days later
#   with no trace back to the write.
#
#   docs/PHILOSOPHY.md 2.4 - registration is how a component joins the WM, and
#   hub_register_component() reports whether the hub took it. A caller that
#   drops the answer leaves the WM missing a feature and says nothing, so the
#   declaration carries warn_unused_result and a dropped answer has to be a
#   diagnostic.
#
# Every fixture under tests/compile-fail/ is built twice:
#
#   with -DREFUSAL_CONTROL   must compile: the code the rule allows
#   without it               must not compile: the code the rule refuses
#
# The control build is what tells a real refusal from a fixture that fails
# because an include path moved. The second build promotes the unused-result
# warning to an error, because without -Werror a fixture that drops
# hub_register_component()'s result would compile and the gate would never see
# it. Both builds stop at the object file (-c, not -fsyntax-only): the
# unused-result diagnostic comes from a pass that -fsyntax-only does not run,
# so a syntax-only gate compiles the fixture it is supposed to refuse.
#
# Exit status: 0 when every fixture behaves, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

CC="${CC:-cc}"
PKGLIST=(xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-keysyms)

if ! command -v "$CC" >/dev/null 2>&1; then
  echo "check-compiler-refusals: $CC is not on PATH" >&2
  exit 1
fi
if ! pkg-config --exists "${PKGLIST[@]}"; then
  echo "check-compiler-refusals: pkg-config cannot find ${PKGLIST[*]}" >&2
  exit 1
fi

read -r -a PKG_CFLAGS <<<"$(pkg-config --cflags "${PKGLIST[@]}")"
FLAGS=(
  "${PKG_CFLAGS[@]}"
  -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include
  -DWM_HUB_TESTING -D_DEFAULT_SOURCE
)
REFUSING_FLAGS=("${FLAGS[@]}" -Werror=unused-result)

shopt -s nullglob
FIXTURES=(tests/compile-fail/*.c)
if [ "${#FIXTURES[@]}" -eq 0 ]; then
  echo "check-compiler-refusals: no fixtures under tests/compile-fail/" >&2
  exit 1
fi

LOG="$(mktemp)"
OBJ="$LOG.o"
trap 'rm -f "$LOG" "$OBJ"' EXIT

FAILED=0
for fixture in "${FIXTURES[@]}"; do
  if ! "$CC" -c -o "$OBJ" "${FLAGS[@]}" -DREFUSAL_CONTROL "$fixture" >"$LOG" 2>&1; then
    echo "check-compiler-refusals: $fixture does not compile even through its control build."
    echo "  The control build proves the fixture and its includes are sound, so this"
    echo "  is a broken fixture or a missing accessor, not a refusal:"
    sed 's/^/    /' "$LOG"
    FAILED=1
    continue
  fi

  if "$CC" -c -o "$OBJ" "${REFUSING_FLAGS[@]}" "$fixture" >"$LOG" 2>&1; then
    echo "check-compiler-refusals: $fixture compiled, and the rule it names must refuse it."
    echo "  Either a machine-tracked field is reachable from a component's translation"
    echo "  unit again, or hub_register_component()'s result can be dropped again."
    FAILED=1
  fi
done

if [ "$FAILED" -ne 0 ]; then
  exit 1
fi

echo "check-compiler-refusals: ${#FIXTURES[@]} fixture(s) refused, their control builds pass"
