#!/usr/bin/env bash
#
# test-113-run-sh-builds-its-tools.sh - a seam run builds the tools it needs.
#
# Every file maps its windows with tools/xclient, and test-86 preloads
# tools/xcb-ewmh-delay.so, so both are build products the run depends on.
# `make test-x` builds them, but run.sh is also documented as runnable on its
# own for one file, and there it refused instead: one make target per round
# trip, while the documentation named only the first. A fresh checkout
# following that documentation stopped twice before it ran a test.
#
# The two are removed here and the run has to put them back. The filter names
# test-109, which starts no client and no WM of its own, so the nested run
# reaches it without depending on the tools this test removed.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

XCLIENT="$ROOT_DIR/tools/xclient"
SHIM="$ROOT_DIR/tools/xcb-ewmh-delay.so"
DIR="$TMPDIR_SEAM/no-seam-tools"
OUT="$TMPDIR_SEAM/run-sh-builds-tools.out"

# The files after this one map their windows with the same client, and test-86
# needs the shim, so both are rebuilt on every exit path. A run that only
# started because they were already there would turn this failure into theirs.
rebuild() { make -s -C "$ROOT_DIR" tools/xclient tools/xcb-ewmh-delay.so >/dev/null 2>&1; }
trap rebuild EXIT

rm -f "$XCLIENT" "$SHIM"

# A TMPDIR of its own: the nested run writes its log under
# $TMPDIR/wm-seam-tests, which is this run's WM log directory too.
mkdir -p "$DIR"
timeout 120 env TMPDIR="$DIR" "$ROOT_DIR/tests/x/run.sh" \
  --filter=test-109 >"$OUT" 2>&1
rc=$?

check_eq "run.sh starts with the seam tools removed" 0 "$rc"
check_eq "run.sh builds tools/xclient" 1 "$([ -x "$XCLIENT" ] && echo 1 || echo 0)"
check_eq "run.sh builds the reply-delay shim" 1 "$([ -f "$SHIM" ] && echo 1 || echo 0)"
if [ "$rc" -ne 0 ]; then
  sed 's/^/    /' "$OUT"
fi

finish
