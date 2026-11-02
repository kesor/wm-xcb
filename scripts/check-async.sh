#!/usr/bin/env bash
#
# check-async.sh - fail on code that can stall the event loop.
#
# Usage:
#   scripts/check-async.sh [-v|--verbose]      # check against the baseline
#   scripts/check-async.sh --update-baseline   # accept today's violations
#
# docs/PHILOSOPHY.md 2.3: nothing in the event loop may wait. This script is the
# mechanical half of that rule. It scans the sources that get compiled into the
# WM and flags calls that block until X or a child process answers.
#
# Two kinds of exception, both explicit:
#
#   1. Waiver: a comment containing "allow-blocking: <reason>" on the line or the
#      two lines above it. The reason says why the wait is safe, usually because
#      it runs at startup before the loop exists.
#   2. Baseline: tests/quality/async-baseline.txt lists the violations that are
#      already known and tracked in beads. A hit in the baseline does not fail
#      the check, so unrelated work is not blocked. A violation that is not in
#      the baseline fails, which is how the count only goes down.
#
# The baseline is written by --update-baseline and reviewed in the diff, the
# same way a golden file is. Entries that no longer occur are reported as stale
# and should be removed with --update-baseline.
#
# Exit status: 0 when there are no new violations, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BASELINE_FILE="$ROOT_DIR/tests/quality/async-baseline.txt"
VERBOSE=0
UPDATE=0

for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    --update-baseline) UPDATE=1 ;;
    -h | --help)
      sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "check-async: unknown argument '$arg'" >&2
      exit 1
      ;;
  esac
done

# Compile the source list exactly as the build does, so a file that is not
# built does not produce findings (wm-kbd-mouse.c is dead but still on disk).
mapfile -t SOURCES < <(make -s print-src 2>/dev/null | tr ' ' '\n' | grep -E '\.c$')
if [ "${#SOURCES[@]}" -eq 0 ]; then
  echo "check-async: could not read the source list from 'make -s print-src'" >&2
  exit 1
fi

# pattern|what the reader should do instead
#
# xcb_poll_for_event() is deliberately not on this list. Returning NULL instead
# of waiting is exactly what an event loop does after poll(2) tells it the
# connection is readable, and a grep cannot tell that drain from the spin it
# replaced. Busy-waiting is measured instead: tests/x/test-85-idle-cpu.sh in the
# seam suite and the idle CPU number in scripts/perf-test.sh.
RULES=(
  'xcb_[a-z_]*_reply\(|synchronous X round trip: use the cookie and handle the reply as an event, or restructure so no reply is needed'
  'xcb_wait_for_reply\(|synchronous X round trip'
  'xcb_request_check\(|synchronous X round trip'
  '\bread\(|blocking read: a callback must not wait for a child process'
  'waitpid\(|blocking wait on a child: reap asynchronously, or pass WNOHANG'
  '\busleep\(|\bsleep\(|nanosleep\(|sleep inside the window manager stalls every client'
)

# Baseline keys are "file|normalized call", not line numbers, so edits that move
# code around do not invalidate the whole file.
normalize() {
  printf '%s' "$1" | sed 's/^[[:space:]]*//; s/[[:space:]][[:space:]]*/ /g; s/[[:space:]]*$//'
}

declare -A BASELINE=()
if [ -f "$BASELINE_FILE" ]; then
  while IFS= read -r line; do
    case "$line" in '' | '#'*) continue ;; esac
    BASELINE["$line"]=1
  done <"$BASELINE_FILE"
fi

NEW_VIOLATIONS=0
KNOWN=0
WAIVED=0
declare -A SEEN=()
declare -a CURRENT=()

for f in "${SOURCES[@]}"; do
  for rule in "${RULES[@]}"; do
    pattern="${rule%%|*}"
    reason="${rule#*|}"
    while IFS= read -r hit; do
      [ -n "$hit" ] || continue
      line="${hit%%:*}"
      text="${hit#*:}"

      # A call named in a comment is not a call.
      stripped="$(printf '%s' "$text" | sed 's/^[[:space:]]*//')"
      case "$stripped" in
        \** | //* | '/*'*) continue ;;
      esac

      # waitpid with WNOHANG does not block.
      if [ "$pattern" = 'waitpid\(' ] && printf '%s' "$text" | grep -q 'WNOHANG'; then
        continue
      fi

      # xcb_poll_for_reply(), xcb_poll_for_reply64() and xcb_discard_reply()
      # are named like the calls that wait, and libxcb documents them as not
      # waiting: the first returns 0 while the reply has not arrived, the last
      # throws a reply away. Flagging them would push the reader to waiver the
      # very call the rule asks for.
      if printf '%s' "$text" | grep -qE 'xcb_(poll_for_reply|poll_for_reply64|discard_reply)\('; then
        continue
      fi

      # A waiver sits on the line or the two above it, with a reason.
      start=$((line - 2))
      [ "$start" -lt 1 ] && start=1
      context=$(sed -n "${start},${line}p" "$f")
      if printf '%s' "$context" | grep -q 'allow-blocking:'; then
        WAIVED=$((WAIVED + 1))
        if [ "$VERBOSE" -eq 1 ]; then
          printf '  waived  %s:%s  %s\n' "$f" "$line" \
            "$(printf '%s' "$context" | grep -o 'allow-blocking:.*' | head -1)"
        fi
        continue
      fi

      key="$f|$(normalize "$text")"
      SEEN["$key"]=1
      CURRENT+=("$key")

      if [ "$UPDATE" -eq 1 ]; then
        continue
      fi

      if [ -n "${BASELINE[$key]:-}" ]; then
        KNOWN=$((KNOWN + 1))
        if [ "$VERBOSE" -eq 1 ]; then
          printf '  known   %s:%s\n' "$f" "$line"
        fi
        continue
      fi

      NEW_VIOLATIONS=$((NEW_VIOLATIONS + 1))
      printf '  NEW VIOLATION  %s:%s  %s\n' "$f" "$line" "$reason"
      printf '                 %s\n' "$(normalize "$text")"
    done < <(grep -nE "$pattern" "$f" 2>/dev/null)
  done
done

if [ "$UPDATE" -eq 1 ]; then
  mkdir -p "$(dirname "$BASELINE_FILE")"
  {
    echo "# Known blocking calls in the window manager event loop."
    echo "#"
    echo "# Generated by scripts/check-async.sh --update-baseline."
    echo "# Each line is <file>|<normalized call>. A violation listed here does not"
    echo "# fail the check; a violation missing from here does. Remove entries when"
    echo "# the call is gone: the script reports stale lines, and the point of the"
    echo "# file is that it shrinks."
    printf '%s\n' "${CURRENT[@]}" | sort -u
  } >"$BASELINE_FILE"
  echo "check-async: baseline updated with $(printf '%s\n' "${CURRENT[@]}" | sort -u | grep -c .) entrie(s) in ${BASELINE_FILE#"$ROOT_DIR/"}"
  exit 0
fi

STALE=0
for key in "${!BASELINE[@]}"; do
  if [ -z "${SEEN[$key]:-}" ]; then
    STALE=$((STALE + 1))
    printf '  stale baseline entry: %s\n' "$key"
  fi
done

echo
if [ "$NEW_VIOLATIONS" -eq 0 ]; then
  echo "check-async: no new violations (${#SOURCES[@]} files, $KNOWN known, $WAIVED waived)"
  if [ "$STALE" -gt 0 ]; then
    echo "check-async: $STALE stale baseline entrie(s); run --update-baseline to drop them"
  fi
  exit 0
fi
echo "check-async: $NEW_VIOLATIONS new violation(s), $KNOWN known, $WAIVED waived"
echo "To accept a new one on purpose:"
echo "  scripts/check-async.sh --update-baseline   (then commit the baseline diff)"
exit 1
