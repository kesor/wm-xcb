#!/usr/bin/env bash
# scripts/xephyr-test.sh has to run the WM variant it was asked for. A release
# build cannot print DEBUG lines - LOG_DEBUG compiles to nothing - so a run
# that asked for DEBUG=1 and left an empty log behind proves the harness ran a
# release binary. The script is run here rather than the WM, because the
# script's own build step is what used to drop the variant.
source "$(dirname "$0")/lib.sh"

OUT="$TMPDIR_SEAM/harness-variant.out"
# The harness starts its own nested server and waits 4 s for windows; a run
# that takes over two minutes is stuck, not slow.
timeout 120 env DEBUG=1 "$ROOT_DIR/scripts/xephyr-test.sh" --windows 1 >"$OUT" 2>&1
rc=$?

check_eq "xephyr-test.sh DEBUG=1 exits 0" 0 "$rc"
check_eq "xephyr-test.sh names the debug variant" 1 \
  "$(grep -c '^xephyr-test.sh: variant=debug ' "$OUT")"
check_eq "xephyr-test.sh runs the debug binary" 1 \
  "$(grep -c "binary=$ROOT_DIR/build/debug/wm," "$OUT")"

HARNESS_LOG=$(sed -n 's/^xephyr-test.sh: .*log=//p' "$OUT" | head -n 1)
check_nonempty "xephyr-test.sh reports its log path" "$HARNESS_LOG"
if [ -f "$HARNESS_LOG" ]; then
  check_ge "the WM log carries DEBUG lines" 1 "$(grep -c '^DEBUG: ' "$HARNESS_LOG")"
else
  fail "the WM log at '$HARNESS_LOG' does not exist"
fi

finish
