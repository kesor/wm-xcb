#!/usr/bin/env bash
# scripts/lib/*.sh have no shebang. shellcheck -x follows them for the names
# they define and does not report their findings, so a gate that lists only
# shebangs never reads them. This drops a library with a known finding and
# expects the gate to fail on it, then removes it and expects the gate to pass.
source "$(dirname "$0")/lib.sh"

PROBE="$ROOT_DIR/scripts/lib/shellcheck-list-probe.sh"
LAST="$TMPDIR_SEAM/shellcheck-list.txt"
cleanup_probe() { rm -f "$PROBE"; }
trap cleanup_probe EXIT

rm -f "$PROBE"
check_eq "the probe starts absent" 0 "$([ -e "$PROBE" ] && echo 1 || echo 0)"

printf '%s\n' '# shellcheck shell=bash' 'cd /tmp' >"$PROBE"
case "$(head -n 1 "$PROBE")" in
  '#!'*) shebang=1 ;;
  *) shebang=0 ;;
esac
check_eq "the probe has no shebang, so a shebang-only list cannot see it" 0 "$shebang"

"$ROOT_DIR/scripts/check-shellcheck.sh" >"$LAST" 2>&1
check_eq "a finding in a shebang-less library fails the gate" 1 "$?"
check_eq "the failure names the probe" 1 "$(grep -cF 'scripts/lib/shellcheck-list-probe.sh' "$LAST")"

cleanup_probe
"$ROOT_DIR/scripts/check-shellcheck.sh" >"$LAST" 2>&1
check_eq "without the probe the gate passes" 0 "$?"
check_eq "the probe leaves nothing behind" 0 "$([ -e "$PROBE" ] && echo 1 || echo 0)"

finish
