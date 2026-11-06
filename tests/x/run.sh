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
# --update-baseline keeps the entries of the files this run did not start, so a
# run with --filter updates the file for the tests it ran and leaves the rest of
# it alone. See scripts/lib/seam-baseline.sh for why, and for the one entry an
# update does drop without measuring it: the one whose test file is gone.
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
# of its assertions passed. One Xephyr serves the whole run, but every file gets
# a WM of its own: a tag switch, a moved client or a changed focus outlives the
# file that made it, and the file after it would fail on state it never set.
# What lives in the server rather than in the WM - the lock modifiers, a RandR
# mode - is still shared, and a test that changes it puts it back.
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
# shellcheck source=../../scripts/lib/seam-baseline.sh
. "$ROOT_DIR/scripts/lib/seam-baseline.sh"

for cmd in Xephyr xdotool xprop xset xwininfo; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "run.sh: missing '$cmd'. Start the dev shell: direnv allow" >&2
    exit 1
  }
done

[ -x "$ROOT_DIR/tools/xclient" ] || {
  echo "run.sh: build the test client first: make tools/xclient" >&2
  exit 1
}

# test-86 dlopens this shim; without it, the test fails and the run reports a
# missing build artifact as a new X-behaviour failure.
[ -f "$ROOT_DIR/tools/xcb-ewmh-delay.so" ] || {
  echo "run.sh: build the reply-delay shim first: make tools/xcb-ewmh-delay.so" >&2
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

# A real dmenu grabs the keyboard for as long as it is open, so a test that
# presses MOD+p and then a key wants a child that holds the pipe open and lets
# the key through: the suite puts stand-ins for the programs the launcher execs
# at the front of PATH. They live in tests/fakes, one file per program, and each
# says on its second line which program it models and on its third a line of
# that program: a stand-in whose name nobody can compare with the program on PATH
# is how three fakes came to model a program the WM does not exec. The set is
# the two stages of `dmenu_path | dmenu`, which is the whole exec line; a stage
# the launcher adds later needs a file here, and the launcher is expected to
# fail without it. test-87 runs the installed dmenu for what a stand-in cannot
# answer for, the line it writes on stdout.
export WM_FAKE_BIN="$ROOT_DIR/tests/fakes"
export PATH="$WM_FAKE_BIN:$PATH"
# Where the fakes record that they ran, so a test can tell an open prompt from a
# key that never reached the launcher.
export WM_FAKE_STARTED_DIR="$LOG_DIR/fake-started"
rm -rf "$WM_FAKE_STARTED_DIR"
mkdir -p "$WM_FAKE_STARTED_DIR"

# Build first, and say which build it is. These scripts start the WM
# themselves, and nothing else rebuilds it, so a stale binary reports the
# behaviour of the tree as it was and the result reads as a WM defect. This
# runs after LOG_DIR exists because the line names the log the run writes, and
# before Xephyr starts because that line is what a reader looks for next.
wm_build_variant "$WM_LOG" || exit 1

# The variant is chosen; nothing below reads DEBUG again, and a shell that
# exports it turns every xdotool call into a 375-line keymap dump on stderr
# (libxdo prints it whenever DEBUG is set to any value). That dump drowned the
# assertions and, at a drag test's call count, overflowed the output this script
# reads back with "Argument list too long". The tests that start their own
# server still need the variant, so it is passed under a name libxdo ignores.
WM_SEAM_DEBUG=${DEBUG:-0}
export WM_SEAM_DEBUG
unset DEBUG SANITIZE

DISPLAY_NUM=""
for n in 2 3 4 5 6 7 8 9; do
  # A lock with no socket is a server that died without cleaning up. Treating it
  # as "in use" removed the display from the pool for every later run.
  if wm_display_free "$n"; then
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
    echo "run.sh: keeping session on $DISPLAY (wm pid ${WM_PID:-none}, parent $WM_PARENT_DISPLAY)"
    return
  fi
  [ -n "$WM_PID" ] && stop_wm
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

# The WM reads a config file at startup, and this suite asserts what the
# compiled one does: the keys its tests press are compiled-in bindings. A
# developer's own file would change what the suite sees depending on whose
# machine ran it, so every WM this harness starts gets a path that does not
# exist. test-54 starts its own WM to check what a file does.
export WM_CONFIG="$LOG_DIR/no-config-file"

# The WM says nothing when it is ready; the root window does. setup_xcb()
# selects SubstructureRedirect on it, and the key grabs are in place before the
# loop reads the first event, so a test that starts here talks to a WM that
# answers keys. The margin after the mask covers the init between the two, which
# is a handful of requests: the WM cannot be observed between them, so a fixed
# wait is the only way to be behind both.
start_wm() {
  stop_wm
  : >"$WM_LOG"
  "$WM_VARIANT_BIN" >"$WM_LOG" 2>&1 &
  WM_PID=$!
  export WM_PID
  local waited=0
  while [ "$waited" -lt 4000 ]; do
    kill -0 "$WM_PID" 2>/dev/null || return 1
    if xwininfo -root -events 2>/dev/null | grep -q SubstructureRedirect; then
      sleep 0.2
      return 0
    fi
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

stop_wm() {
  [ -n "$WM_PID" ] || return 0
  # SIGKILL, not SIGTERM: a test that fails between kill -STOP and kill -CONT
  # leaves the WM stopped, and a stopped process keeps SIGTERM pending.
  kill -KILL "$WM_PID" 2>/dev/null
  wait "$WM_PID" 2>/dev/null
  WM_PID=""
}

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

# What this run measured, in the shape the baseline functions take: the test
# files it started, and every failure it saw - the known ones and the new ones
# alike, because an assertion that failed again is not stale. The files it did
# not start are the ones a --filter left out, and they are what a filtered
# update has to leave alone.
STARTED_FILE="$LOG_DIR/seam-started-files.txt"
MEASURED_FILE="$LOG_DIR/seam-measured-keys.txt"
for f in "${TESTS[@]}"; do basename "$f"; done >"$STARTED_FILE"
: >"$MEASURED_FILE"

# A failure is identified by the test file and the assertion label, which is the
# text before the first colon: the numbers after it change from run to run.
fail_key() { # fail_key <file> <fail text>
  local file=$1 text=$2
  text=${text#  FAIL  }
  text=${text%%:*}
  text=$(printf '%s' "$text" | sed 's/[[:space:]]*$//')
  printf '%s|%s' "$file" "$text"
}

echo "seam tests on $DISPLAY, ${#TESTS[@]} file(s)"
TOTAL_PASS=0
TOTAL_KNOWN=0
TOTAL_NEW=0
FAILED_FILES=()

for f in "${TESTS[@]}"; do
  name=$(basename "$f")
  if ! start_wm; then
    echo "run.sh: the WM did not come up for $name:" >&2
    cat "$WM_LOG" >&2
    exit 1
  fi
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
    printf '%s\n' "$key" >>"$MEASURED_FILE"
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
  # A test that killed the WM loses the log the next file truncates, so the log
  # of the WM that died is kept under its own name. It does not stop the run:
  # the next file starts on a WM of its own.
  if ! kill -0 "$WM_PID" 2>/dev/null; then
    crashed_log="$LOG_DIR/wm-crashed-$name.log"
    cp "$WM_LOG" "$crashed_log" 2>/dev/null
    echo "  the WM died during $name; its log is $crashed_log"
    TOTAL_NEW=$((TOTAL_NEW + 1))
    FAILED_FILES+=("$name")
  fi
done

echo
if [ "$UPDATE_BASELINE" -eq 1 ]; then
  # Rename in the baseline's directory, not a copy out of the log directory:
  # mv across filesystems unlinks the baseline before the copy.
  seam_baseline_install "$BASELINE" "$ROOT_DIR/tests/x" "$STARTED_FILE" "$MEASURED_FILE" || exit 1
  echo "seam tests: baseline updated with $(seam_baseline_entries "$BASELINE" | grep -c .) entries"
  seam_baseline_filter_note "$FILTER" "${#TESTS[@]}"
  exit 0
fi

STALE=0
while IFS= read -r key; do
  [ -n "$key" ] || continue
  STALE=$((STALE + 1))
  printf '  stale baseline entry: %s\n' "$key"
done < <(seam_baseline_stale "$BASELINE" "$ROOT_DIR/tests/x" "$STARTED_FILE" "$MEASURED_FILE")

echo "seam tests: $TOTAL_PASS passed, $TOTAL_KNOWN known failed, $TOTAL_NEW new failed, logs in $LOG_DIR"
if [ "$STALE" -gt 0 ]; then
  echo "seam tests: $STALE stale baseline entries; run --update-baseline to drop them"
fi
if [ "$TOTAL_NEW" -gt 0 ]; then
  echo "new failures: ${FAILED_FILES[*]}"
  exit 1
fi
exit 0
