#!/usr/bin/env bash
#
# check-harness-variant.sh - a harness must build and run the WM variant it was
# asked for, and say which one that is.
#
# Usage: scripts/check-harness-variant.sh [--verbose]
#
# scripts/xephyr-test.sh and tests/x/run.sh start the WM themselves, so they
# build it themselves. A plain `make wm` in such a script builds whatever the
# Makefile's default is, which is release, so a DEBUG=1 run got a release binary,
# an empty log, and the conclusion that the event never happened. Two workers
# paid for that discovery by hand-rolling a Xephyr session instead.
#
# So this asks the shared builder, scripts/lib/wm-variant.sh, for each variant
# and checks four things: the line it prints names the variant, the binary and
# the log; the binary it names is the one that variant produces; neither
# harness builds the WM behind its back; and ./wm is left alone. The `wm`
# target copies onto ./wm, and the builder must not use it: a gate whose last
# case is SANITIZE=1 would otherwise leave the ASan binary where a manual ./wm
# starts. A release binary cannot carry the DEBUG log literals (LOG_DEBUG
# compiles to nothing) and an ASan binary carries __asan_init, so both are
# visible in the binary itself without starting a server.
#
# The end-to-end half - a DEBUG=1 harness run whose log actually holds DEBUG
# lines - lives in tests/x/test-90-harness-variant.sh, because it needs a
# display. This script runs anywhere.
#
# Exit status: 0 when the harnesses and the builder agree, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
case "${1:-}" in
  -v | --verbose) VERBOSE=1 ;;
  -h | --help)
    awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit 0
    ;;
  "")
    ;;
  *)
    echo "check-harness-variant: unknown argument '$1'" >&2
    exit 1
    ;;
esac

FOUND=0
fail() {
  FOUND=$((FOUND + 1))
  echo "  FAIL  $*"
}
pass() {
  [ "$VERBOSE" -eq 1 ] && echo "  ok    $*"
  return 0
}

# shellcheck source=lib/wm-variant.sh
. "$ROOT_DIR/scripts/lib/wm-variant.sh"

TMP_LOG="$(mktemp)"
trap 'rm -f "$TMP_LOG"' EXIT

HARNESSES=(scripts/xephyr-test.sh tests/x/run.sh)

# A harness that calls make itself can build a variant other than the one it
# was asked for, which is the defect this gate exists for. The builder is the
# only place allowed to run it.
for harness in "${HARNESSES[@]}"; do
  if ! grep -q 'wm_build_variant' "$harness"; then
    fail "$harness does not build through scripts/lib/wm-variant.sh, so its variant is whatever it chooses"
    continue
  fi
  if ! grep -qF '"$WM_VARIANT_BIN"' "$harness"; then
    fail "$harness does not start the binary the builder names"
  fi
  pass "$harness builds through the shared builder"
  while IFS= read -r hit; do
    fail "$harness runs make itself: ${hit#"$harness:"}"
  done < <(grep -nE '^[^#]*\b(make|gmake)\b[^#]*\bwm\b' "$harness")
done

# The builder must not replace ./wm. The harnesses run build/<variant>/wm.
# The `wm` target copies onto ./wm, so a DEBUG=1 harness run, and this gate,
# would replace the binary a later manual start picks up.
ROOT_WM="$ROOT_DIR/wm"
if [ -e "$ROOT_WM" ]; then
  WM_BEFORE=$(md5sum "$ROOT_WM")
else
  WM_BEFORE="absent"
fi

# variant | DEBUG | SANITIZE | marker inside the binary
CASES=(
  "release|0|0|none"
  "debug|1|0|debug-log"
  "release-sanitize|0|1|asan"
)

for spec in "${CASES[@]}"; do
  IFS='|' read -r want_variant debug sanitize marker <<<"$spec"
  if ! DEBUG="$debug" SANITIZE="$sanitize" wm_build_variant "$TMP_LOG"; then
    fail "DEBUG=$debug SANITIZE=$sanitize did not build"
    continue
  fi
  line=$WM_VARIANT_LINE

  want_bin="$ROOT_DIR/build/$want_variant/wm"
  case "$line" in
    *"variant=$want_variant "*) pass "DEBUG=$debug SANITIZE=$sanitize reports variant=$want_variant" ;;
    *) fail "DEBUG=$debug SANITIZE=$sanitize reports '$line', expected variant=$want_variant" ;;
  esac
  case "$line" in
    *"binary=$want_bin,"*) pass "reports the binary path" ;;
    *) fail "DEBUG=$debug SANITIZE=$sanitize names a binary other than $want_bin: $line" ;;
  esac
  case "$line" in
    *"log=$TMP_LOG") pass "reports the log path" ;;
    *) fail "DEBUG=$debug SANITIZE=$sanitize does not report log=$TMP_LOG: $line" ;;
  esac
  if [ -x "$WM_VARIANT_BIN" ]; then
    pass "$want_variant binary exists"
  else
    fail "DEBUG=$debug SANITIZE=$sanitize left no binary at $WM_VARIANT_BIN"
    continue
  fi

  # grep -c, not grep -q: -q exits at the first match and the SIGPIPE reaches
  # strings, which under pipefail turns a found marker into a failed pipeline.
  has_debug=$(strings "$WM_VARIANT_BIN" | grep -c '^DEBUG: ' || true)
  has_asan=$(nm "$WM_VARIANT_BIN" 2>/dev/null | grep -c '__asan_init' || true)
  case "$marker" in
    debug-log)
      [ "${has_debug:-0}" -gt 0 ] ||
        fail "$WM_VARIANT_BIN carries no DEBUG log strings, so it cannot log what DEBUG=1 was for"
      [ "${has_asan:-0}" -eq 0 ] || fail "$WM_VARIANT_BIN is instrumented; SANITIZE was not set"
      ;;
    asan)
      [ "${has_asan:-0}" -gt 0 ] ||
        fail "$WM_VARIANT_BIN has no ASan symbols; SANITIZE=1 built the plain objects"
      [ "${has_debug:-0}" -eq 0 ] || fail "$WM_VARIANT_BIN carries DEBUG log strings; DEBUG was not set"
      ;;
    none)
      [ "${has_debug:-0}" -eq 0 ] || fail "$WM_VARIANT_BIN carries DEBUG log strings; it is not the release build"
      [ "${has_asan:-0}" -eq 0 ] || fail "$WM_VARIANT_BIN is instrumented; it is not the release build"
      ;;
  esac
  pass "$want_variant binary is what the line says it is"
done

if [ -e "$ROOT_WM" ]; then
  WM_AFTER=$(md5sum "$ROOT_WM")
else
  WM_AFTER="absent"
fi
if [ "$WM_BEFORE" = "$WM_AFTER" ]; then
  pass "./wm was not replaced"
else
  fail "the builder replaced ./wm ($WM_BEFORE -> $WM_AFTER); build build/<variant>/wm, not the wm copy target"
fi

if [ "$FOUND" -eq 0 ]; then
  echo "check-harness-variant: both harnesses build and report the variant they were asked for"
  exit 0
fi
echo "check-harness-variant: $FOUND problem(s)."
echo "A harness that builds the WM itself decides which variant runs, and a"
echo "DEBUG=1 run that quietly runs release writes an empty log. Build through"
echo "scripts/lib/wm-variant.sh and let it print the variant, binary and log."
exit 1
