#!/usr/bin/env bash
#
# check-async.sh - fail on code that can stall the event loop.
#
# Usage:
#   scripts/check-async.sh [-v|--verbose]
#
# docs/PHILOSOPHY.md 2.3: nothing in the event loop may wait. This script is the
# mechanical half of that rule. It scans the sources that get compiled into the
# WM and flags calls that block until X or a child process answers.
#
# A line is waived when it, or one of the two preceding lines, carries a comment
# containing "allow-blocking: <reason>". The marker is the record of why the
# wait is safe, usually because it happens during startup, before the loop runs.
#
# Exit status: 0 when every blocking call is waived, 1 otherwise.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

VERBOSE=0
case "${1:-}" in
  -v | --verbose) VERBOSE=1 ;;
  -h | --help)
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  "") ;;
  *)
    echo "check-async: unknown argument '$1'" >&2
    exit 1
    ;;
esac

# Compile the source list exactly as the build does, so a file that is not
# built does not produce findings (wm-kbd-mouse.c is dead but still on disk).
mapfile -t SOURCES < <(make -s print-src 2>/dev/null | tr ' ' '\n' | grep -E '\.c$')
if [ "${#SOURCES[@]}" -eq 0 ]; then
  echo "check-async: could not read the source list from 'make -s print-src'" >&2
  exit 1
fi

# pattern|what the reader should do instead
RULES=(
  'xcb_[a-z_]*_reply\(|synchronous X round trip: use the cookie and handle the reply as an event, or restructure so no reply is needed'
  'xcb_wait_for_reply\(|synchronous X round trip'
  'xcb_request_check\(|synchronous X round trip'
  'xcb_poll_for_event\(|polling instead of blocking burns a core: block in xcb_wait_for_event() or poll(2)'
  '\bread\(|blocking read: a callback must not wait for a child process'
  'waitpid\(|blocking wait on a child: reap asynchronously, or pass WNOHANG'
  '\busleep\(|\bsleep\(|nanosleep\(|sleep inside the window manager stalls every client'
)

VIOLATIONS=0
WAIVED=0

for f in "${SOURCES[@]}"; do
  for rule in "${RULES[@]}"; do
    pattern="${rule%%|*}"
    reason="${rule#*|}"
    while IFS= read -r hit; do
      [ -n "$hit" ] || continue
      line="${hit%%:*}"
      text="${hit#*:}"

      # A call named in a comment is not a call. Comments explaining why a wait
      # is safe are exactly what the marker section below asks for.
      stripped="$(printf '%s' "$text" | sed 's/^[[:space:]]*//')"
      case "$stripped" in
        \** | //* | '/*'*) continue ;;
      esac

      # waitpid with WNOHANG does not block.
      if [ "$pattern" = 'waitpid\(' ] && printf '%s' "$text" | grep -q 'WNOHANG'; then
        continue
      fi

      # The waiver may sit on the line itself or in the two lines above it.
      start=$((line - 2))
      [ "$start" -lt 1 ] && start=1
      context=$(sed -n "${start},${line}p" "$f")
      if printf '%s' "$context" | grep -q 'allow-blocking:'; then
        WAIVED=$((WAIVED + 1))
        if [ "$VERBOSE" -eq 1 ]; then
          printf '  waived     %s:%s  %s\n' "$f" "$line" \
            "$(printf '%s' "$context" | grep -o 'allow-blocking:.*' | head -1)"
        fi
        continue
      fi

      VIOLATIONS=$((VIOLATIONS + 1))
      printf '  VIOLATION  %s:%s  %s\n' "$f" "$line" "$reason"
      printf '             %s\n' "$(printf '%s' "$text" | sed 's/^[[:space:]]*//')"
    done < <(grep -nE "$pattern" "$f" 2>/dev/null)
  done
done

echo
if [ "$VIOLATIONS" -eq 0 ]; then
  echo "check-async: no violations in ${#SOURCES[@]} files ($WAIVED waived)"
  exit 0
fi
echo "check-async: $VIOLATIONS violation(s) in ${#SOURCES[@]} files ($WAIVED waived)"
echo "Each one is listed in a beads issue: 'bd list --parent wm-46k.2'."
exit 1
