#!/usr/bin/env bash
#
# run.sh - run the seam tests against a live WM in a nested X server.
#
# Usage:
#   tests/x/run.sh [--filter PATTERN] [--keep] [--verbose]
#
#   --filter PATTERN      only run test files whose name contains PATTERN
#   --filter=PATTERN      same, the form docs/TESTING.md shows
#   --keep                leave Xephyr and the WM running afterwards
#   --verbose             echo the WM log after a failed test
#   --update-baseline     accept today's failures as known ones
#
# The parent display Xephyr draws into is a private Xvfb this run starts, so the
# run leaves the session alone; see scripts/lib/private-display.sh. That display
# is killed with the run, so a session kept for inspection needs the session's
# display as well:
#   WM_USE_SESSION_DISPLAY=1 make test-x TEST_X_ARGS=--keep
#
# The WM it runs is the variant named by DEBUG and SANITIZE in the environment,
# exactly as `make DEBUG=1` and `make SANITIZE=1` build it:
#   make DEBUG=1 test-x
# The first line of output names the parent display the run nests into, and the
# second the variant, the binary and the log, so a run never leaves you guessing
# which binary it was, where its log went, or what it drew on.
#
# A failure that is listed in tests/quality/seam-baseline.txt is a known failure:
# it is reported and does not fail the run. A failure that is not listed is new
# and exits non-zero. The seam suite covers parts of the WM that are deliberately
# unwired, so without a baseline every run is red and nobody can tell a
# regression from the standing list.
#
# Each test file is tests/x/test-*.sh. It gets DISPLAY, WM_PID, WM_LOG and
# ROOT_DIR in its environment, sources tests/x/lib.sh, and exits 0 only if all
# of its assertions passed.
#
# Exit status: 0 if every test passed, 1 otherwise.
#
# The suite asserts what the X server sees. Assertions that still fail on
# purpose are listed in tests/quality/seam-baseline.txt; a failure missing
# from that file is new.

set -uo pipefail

# The parent-display helper re-executes this script, so it needs the arguments
# from before the loop below consumed them.
SCRIPT_ARGS=("$@")

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export ROOT_DIR

FILTER=""
KEEP=0
VERBOSE=0
UPDATE_BASELINE=0
SCREEN_W=1280
SCREEN_H=800

while [ $# -gt 0 ]; do
  case "$1" in
    --filter) # --filter NAME
      FILTER="$2"
      shift 2
      ;;
    --filter=*) # --filter=NAME, the form docs/TESTING.md shows
      FILTER="${1#--filter=}"
      shift
      ;;
    --keep)
      KEEP=1
      shift
      ;;
    --verbose)
      VERBOSE=1
      shift
      ;;
    --update-baseline)
      UPDATE_BASELINE=1
      shift
      ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *)
      echo "run.sh: unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done

# shellcheck source=../../scripts/lib/wm-variant.sh
. "$ROOT_DIR/scripts/lib/wm-variant.sh"
# shellcheck source=../../scripts/lib/private-display.sh
. "$ROOT_DIR/scripts/lib/private-display.sh"

for cmd in Xephyr xdotool xprop xset; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "run.sh: missing '$cmd'. Start the dev shell: direnv allow" >&2
    exit 1
  }
done

[ -x "$ROOT_DIR/tools/xclient" ] || {
  echo "run.sh: build the test client first: make tools/xclient" >&2
  exit 1
}

# Xephyr draws into a parent display, and that display is a private Xvfb this
# run starts rather than the one the caller is sitting in. The line naming it is
# printed and recorded next to the logs, so a reader can tell a hermetic run from
# one that drew on the session; test-91 asserts the recorded line matches the
# display the run handed to its tests. This runs before the build because the
# helper re-executes this script, and a build run twice prints the variant line
# twice.
wm_parent_display "${SCRIPT_ARGS[@]}" || exit 1

LOG_DIR="${TMPDIR:-/tmp}/wm-seam-tests"
mkdir -p "$LOG_DIR"
export WM_LOG="$LOG_DIR/wm.log"
export TMPDIR_SEAM="$LOG_DIR"
XEPHYR_LOG="$LOG_DIR/xephyr.log"
printf '%s\n' "$WM_PARENT_DISPLAY_LINE" >"$LOG_DIR/parent-display.txt"

# A real dmenu needs fonts and a keyboard grab that a headless nested server does
# not reliably provide. The suite replaces it with a child that writes nothing
# and stays open for longer than any test runs: that is what the WM sees while a
# user looks at the prompt, and it is the only dmenu a test should meet. The WM
# runs `dmenu_path | dmenu`, so both names have to resolve here.
FAKE_BIN="$LOG_DIR/bin"
mkdir -p "$FAKE_BIN"
cat >"$FAKE_BIN/dmenu" <<FAKE_DMENU
#!/bin/sh
: >"$LOG_DIR/dmenu-started"
sleep 5
FAKE_DMENU
chmod +x "$FAKE_BIN/dmenu"
cat >"$FAKE_BIN/dmenu_path" <<'FAKE_DMENU_PATH'
#!/bin/sh
:
FAKE_DMENU_PATH
chmod +x "$FAKE_BIN/dmenu_path"
export PATH="$FAKE_BIN:$PATH"
# Where the fake dmenu records that it started, so a test can tell an open
# prompt from a key that never reached the launcher.
export WM_SEAM_DMENU_STARTED="$LOG_DIR/dmenu-started"

# Build first, and say which build it is. These scripts start the WM
# themselves, and nothing else rebuilds it, so a stale binary reports the
# behaviour of the tree as it was and the result reads as a WM defect. This
# runs after LOG_DIR exists because the line names the log the run writes, and
# before Xephyr starts because that line is what a reader looks for next.
wm_build_variant "$WM_LOG" || exit 1

DISPLAY_NUM=""
for n in 2 3 4 5 6 7 8 9; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
[ -n "$DISPLAY_NUM" ] || {
  echo "run.sh: no free X display in :2..:9" >&2
  exit 1
}
export DISPLAY=":$DISPLAY_NUM"

XEPHYR_PID=""
WM_PID=""

cleanup() {
  if [ "$KEEP" -eq 1 ]; then
    echo "run.sh: keeping session on $DISPLAY (wm pid $WM_PID, parent $WM_PARENT_DISPLAY)"
    return
  fi
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$XEPHYR_PID" ] && kill "$XEPHYR_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

DISPLAY="$WM_PARENT_DISPLAY" Xephyr "$DISPLAY" -screen "${SCREEN_W}x${SCREEN_H}" -noreset \
  >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!

for _ in $(seq 1 50); do
  xdotool getdisplaygeometry >/dev/null 2>&1 && break
  sleep 0.1
done
xdotool getdisplaygeometry >/dev/null 2>&1 || {
  echo "run.sh: Xephyr did not start on $DISPLAY" >&2
  cat "$XEPHYR_LOG" >&2
  exit 1
}

"$WM_VARIANT_BIN" >"$WM_LOG" 2>&1 &
WM_PID=$!
sleep 1
kill -0 "$WM_PID" 2>/dev/null || {
  echo "run.sh: WM exited during startup:" >&2
  cat "$WM_LOG" >&2
  exit 1
}
export WM_PID

TESTS=()
for f in "$ROOT_DIR"/tests/x/test-*.sh; do
  [ -e "$f" ] || continue
  if [ -n "$FILTER" ] && [[ "$f" != *"$FILTER"* ]]; then
    continue
  fi
  TESTS+=("$f")
done

BASELINE="$ROOT_DIR/tests/quality/seam-baseline.txt"
declare -A BASELINE_KEYS=()
if [ -f "$BASELINE" ] && [ "$UPDATE_BASELINE" -eq 0 ]; then
  while IFS= read -r line; do
    case "$line" in '' | '#'*) continue ;; esac
    BASELINE_KEYS["$line"]=1
  done <"$BASELINE"
fi

# A failure is identified by the test file and the assertion label, which is the
# text before the first colon: the numbers after it change from run to run.
fail_key() { # fail_key <file> <fail text>
  local file=$1 text=$2
  text=${text#  FAIL  }
  text=${text%%:*}
  text=$(printf '%s' "$text" | sed 's/[[:space:]]*$//')
  printf '%s|%s' "$file" "$text"
}

declare -A SEEN_KEYS=()
declare -a CURRENT_KEYS=()

echo "seam tests on $DISPLAY, ${#TESTS[@]} file(s)"
TOTAL_PASS=0
TOTAL_KNOWN=0
TOTAL_NEW=0
FAILED_FILES=()

for f in "${TESTS[@]}"; do
  name=$(basename "$f")
  printf '\n%s\n' "$name"
  out=$(bash "$f" 2>&1)
  rc=$?
  printf '%s\n' "$out"
  pass_here=$(printf '%s\n' "$out" | grep -c '^  ok ')
  TOTAL_PASS=$((TOTAL_PASS + pass_here))
  new_here=0

  while IFS= read -r line; do
    [ -n "$line" ] || continue
    key=$(fail_key "$name" "$line")
    SEEN_KEYS["$key"]=1
    CURRENT_KEYS+=("$key")
    if [ "$UPDATE_BASELINE" -eq 1 ]; then
      continue
    fi
    if [ -n "${BASELINE_KEYS[$key]:-}" ]; then
      TOTAL_KNOWN=$((TOTAL_KNOWN + 1))
      printf '  known FAIL  %s\n' "$(printf '%s' "$line" | sed 's/^  FAIL  //')"
    else
      TOTAL_NEW=$((TOTAL_NEW + 1))
      new_here=$((new_here + 1))
      printf '  NEW FAIL    %s\n' "$(printf '%s' "$line" | sed 's/^  FAIL  //')"
    fi
  done < <(printf '%s\n' "$out" | grep '^  FAIL ')

  if [ "$new_here" -gt 0 ]; then
    FAILED_FILES+=("$name")
    if [ "$VERBOSE" -eq 1 ]; then
      echo "  --- WM log tail ---"
      tail -n 25 "$WM_LOG" | sed 's/^/        /'
    fi
  fi
  # A test that killed the WM stops the run: every later test would report the
  # same failure.
  if ! kill -0 "$WM_PID" 2>/dev/null; then
    echo "  WM died during $name; stopping the run"
    TOTAL_NEW=$((TOTAL_NEW + 1))
    break
  fi
done

echo
if [ "$UPDATE_BASELINE" -eq 1 ]; then
  mkdir -p "$(dirname "$BASELINE")"
  {
    echo "# Seam assertions that fail on a tree where the WM is not wired yet."
    echo "#"
    echo "# Generated by tests/x/run.sh --update-baseline."
    echo "# Each line is <test file>|<assertion label>. A failure listed here is"
    echo "# known and does not fail the run; a failure missing from here is new."
    echo "# Removing an entry is how a fixed assertion stops being excused: the run"
    echo "# reports stale entries, and the list is meant to shrink."
    printf '%s\n' "${CURRENT_KEYS[@]}" | sort -u
  } >"$BASELINE"
  echo "seam tests: baseline updated with $(printf '%s\n' "${CURRENT_KEYS[@]}" | sort -u | grep -c .) entries"
  exit 0
fi

STALE=0
for key in "${!BASELINE_KEYS[@]}"; do
  if [ -z "${SEEN_KEYS[$key]:-}" ]; then
    STALE=$((STALE + 1))
    printf '  stale baseline entry: %s\n' "$key"
  fi
done

echo "seam tests: $TOTAL_PASS passed, $TOTAL_KNOWN known failed, $TOTAL_NEW new failed, logs in $LOG_DIR"
if [ "$STALE" -gt 0 ]; then
  echo "seam tests: $STALE stale baseline entries; run --update-baseline to drop them"
fi
if [ "$TOTAL_NEW" -gt 0 ]; then
  echo "new failures: ${FAILED_FILES[*]}"
  exit 1
fi
exit 0
