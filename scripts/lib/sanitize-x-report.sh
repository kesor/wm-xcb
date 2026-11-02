# shellcheck shell=bash
#
# lib/sanitize-x-report.sh - judge one recorded sanitize-x run.
#
# Sourced by scripts/check-sanitize-x.sh, which starts the harness and then asks
# this what came out of it, and by tests/x/test-95-sanitize-x-report.sh, which
# feeds it recorded output instead of starting a nested X server.
#
# ROOT_DIR must be set. The baseline comparison, the site key and the scrub of
# the caller's sanitizer options live in scripts/lib/sanitize-report.sh, which
# the unit-suite gate reads too: two gates judging the same report with two
# copies of the verdict is how one of them stays green while the other is
# wrong. What stays here is the part only this gate has, the harness line that
# says which build ran.
#
# The gate exists because a stack-buffer-overflow in the map path survived every
# other gate. A gate that can be silenced answers the same as one that found
# nothing, so a harness line naming an uninstrumented variant is refused here,
# and the shapes that silence a report are refused in the shared library.

# shellcheck source=sanitize-report.sh
. "$ROOT_DIR/scripts/lib/sanitize-report.sh"

# sanitize_x_trust <harness-output>
#   Prints the log path the harness named, and returns non-zero with the reason
#   on stderr when the run is not worth reading: wrong variant, no readable log,
#   or a fatal AddressSanitizer error.
sanitize_x_trust() {
  local out=$1 variant log
  variant=$(sed -n 's/^[^:]*: variant=\([^ ]*\) .*/\1/p' "$out" | head -n 1)
  if [ "$variant" != "release-sanitize" ]; then
    echo "check-sanitize-x: the harness reported variant='${variant:-none}', not release-sanitize." >&2
    echo "check-sanitize-x: an uninstrumented WM writes a log with no report in it, which reads as a clean run." >&2
    echo "check-sanitize-x: the log is only worth judging when the sanitizer wrote it." >&2
    return 1
  fi
  log=$(sed -n 's/^[^:]*: .*log=//p' "$out" | head -n 1)
  if [ -z "$log" ] || [ ! -f "$log" ]; then
    echo "check-sanitize-x: the harness named no readable log ('${log:-}'), so the run cannot be checked" >&2
    return 1
  fi
  # A fatal ASan error is never accepted: an entry in the baseline says "this
  # allocation site is known", not "reading past a buffer is fine".
  if grep -q 'ERROR: AddressSanitizer:' "$log"; then
    echo "check-sanitize-x: the WM log carries an AddressSanitizer error:" >&2
    grep -A12 -m1 'ERROR: AddressSanitizer:' "$log" >&2
    echo "check-sanitize-x: see $log" >&2
    return 1
  fi
  printf '%s\n' "$log"
}

# sanitize_x_verify <harness-output> <baseline>
#   Returns 0 when the run is acceptable and 1 when it is not, printing why.
sanitize_x_verify() {
  local out=$1 baseline=$2 log
  log=$(sanitize_x_trust "$out") || return 1
  sanitize_verify_sites "$log" "$baseline" check-sanitize-x
}
