#!/usr/bin/env bash
#
# gate-log.sh - run a gate with its output captured, printed only on failure.
#
# A gate that passes has one line to say. A gate that fails has the whole log:
# the failing assertion, the compiler error, the warning that came before it.
# The runner prints one line per assertion and gcc prints one line per file, so
# a passing run of a build-and-test gate is a wall of green that hides the line
# that matters, and every push carries it.
#
# Source this file, then:
#
#   gate_log "check-unit" make test
#   gate_log "check-unit" make DEBUG=1
#
# Exit status is the command's, so `gate_log ... || exit 1` works.

gate_log() {
  local label=$1
  shift
  local log
  log=$(mktemp) || {
    echo "$label: cannot create a log file" >&2
    return 1
  }
  if "$@" >"$log" 2>&1; then
    rm -f "$log"
    return 0
  fi
  cat "$log" >&2
  echo "$label: '$*' failed; its output is above." >&2
  rm -f "$log"
  return 1
}
