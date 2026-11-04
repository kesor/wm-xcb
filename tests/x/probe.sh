#!/usr/bin/env bash
#
# probe.sh - print what the WM does to the windows, on a display of its own.
#
# Usage:
#   tests/x/probe.sh [-n N] [-W WIDTH] [-H HEIGHT] [-c CMD] [-w MS] [-l LINES]
#
#   -n N       how many windows to map (default 2)
#   -W WIDTH   requested width of each window (default 400)
#   -H HEIGHT  requested height of each window (default 300)
#   -c CMD     run CMD against the display, after the windows are mapped and
#              before the state is printed. It runs in `bash -c` with DISPLAY
#              set, so it is one command; a whole script is
#              -c 'bash /path/to/script.sh'. Several steps are separated by
#              ';' or newlines inside the one string
#   -w MS      wait this long after the windows are mapped, and again after CMD
#              (default 500). The WM answers asynchronously, so reading the
#              state without a wait measures the race, not the WM
#   -l LINES   lines of the WM log to print at the end (default 20)
#
# Every question a seam test asks starts with "what does the WM do here?", and
# the answer was a throwaway script in /tmp that composed lib.sh, started a
# server and printed some of it. The next question rewrote the script, because
# it died with the session and never entered the repository. This is that script
# written once: it starts a private Xvfb, draws a Xephyr into it, starts the WM
# the way tests/x/run.sh does, maps the windows, runs CMD if there is one, and
# prints the state every seam assertion reads.
#
# It is a tool, not a test: run.sh discovers tests/x/test-*.sh, so this file
# never runs as part of the suite. A tool also has to be usable on a defect
# nobody has written a test for yet, so it reports rather than asserts: the exit
# status says whether the run itself worked, not whether the WM is correct.
#
# Output shape, one line each and always in this order. The command section is
# there only when -c was given, and the WM log is whatever the WM wrote.
#
#   probe.sh: parent display = :99 (private Xvfb), ...
#   probe.sh: variant=release (DEBUG=0 SANITIZE=0), binary=..., log=...
#   probe: display=:2 config=<path|none>
#   --- command: xdotool getdisplaygeometry ---
#   1280 800
#   command: exit 0
#   --- state ---
#   screen: 1280 800
#   window          x     y     w     h  border  viewable  focus  tags  title
#   0x00400000    640     0   638   798       1       yes     no     -  probe-1
#   0x00400001      0     0   638   798       1       yes    yes     -  probe-2
#   viewable: 2 of 2
#   --- root properties ---
#   _NET_CLIENT_LIST: not set
#   --- wm ---
#   wm: running (pid 1234)
#   --- wm log (last 20 lines) ---
#
# viewable and focus come from the X server. tags is the client's
# _NET_WM_DESKTOP, and prints '-' when the WM sets no such property, which is
# where it stands while no client-side EWMH property is published; the root
# properties section says the same about the root window. A probe that leaves
# that out of its own output makes the reader start a second one to find out.
#
# The pointer starts at the centre of the screen, which is inside one of the
# windows. A warp into the window it is already in sends no enter event, so a
# focus question needs a move away first: two moves inside one CMD, or the
# answer reads "focus did not follow" when nothing asked it to.
#
# Exit status: the command's, when one was given and the WM is still running; 1
# when the WM died or the run could not start; 0 otherwise.

set -uo pipefail

# wm_parent_display re-executes this script, so it needs the arguments from
# before the loop below consumed them.
SCRIPT_ARGS=("$@")

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export ROOT_DIR

N=2
WIDTH=400
HEIGHT=300
COMMAND=""
WAIT_MS=500
LOG_LINES=20
SCREEN_W=1280
SCREEN_H=800

usage() {
  awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
}

while [ $# -gt 0 ]; do
  case "$1" in
    -n)
      N="$2"
      shift 2
      ;;
    -W)
      WIDTH="$2"
      shift 2
      ;;
    -H)
      HEIGHT="$2"
      shift 2
      ;;
    -c)
      COMMAND="$2"
      shift 2
      ;;
    -w)
      WAIT_MS="$2"
      shift 2
      ;;
    -l)
      LOG_LINES="$2"
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "probe: unknown argument '$1'" >&2
      echo "probe: -h prints the usage" >&2
      exit 1
      ;;
  esac
done

for cmd in Xephyr xdotool xprop xwininfo; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "probe: missing required command '$cmd'. Start the dev shell: direnv allow" >&2
    exit 1
  fi
done
if [ ! -x "$ROOT_DIR/tools/xclient" ]; then
  echo "probe: build the test client first: make tools/xclient" >&2
  exit 1
fi

# shellcheck source=../../scripts/lib/wm-variant.sh
. "$ROOT_DIR/scripts/lib/wm-variant.sh"
# shellcheck source=../../scripts/lib/private-display.sh
. "$ROOT_DIR/scripts/lib/private-display.sh"

LOG_DIR="${TMPDIR:-/tmp}/wm-probe"
mkdir -p "$LOG_DIR"
WM_LOG="$LOG_DIR/wm.log"
XEPHYR_LOG="$LOG_DIR/xephyr.log"
export WM_LOG
export TMPDIR_SEAM="$LOG_DIR"

# Before the build: the helper re-executes this script, and a build run twice
# prints the variant line twice, which reads as two runs.
wm_parent_display "${SCRIPT_ARGS[@]}" || exit 1

# The WM it starts is the variant DEBUG and SANITIZE name, and the build says
# which one that was. A probe that reported the behaviour of a stale binary
# would turn "the WM does nothing" into a defect report.
wm_build_variant "$WM_LOG" || exit 1

DISPLAY_NUM=""
for n in 2 3 4 5 6 7 8 9; do
  # wm_display_free clears a lock whose server is gone and says so, instead of
  # skipping that display for every run from then on.
  if wm_display_free "$n"; then
    DISPLAY_NUM=$n
    break
  fi
done
if [ -z "$DISPLAY_NUM" ]; then
  echo "probe: no free X display in :2..:9" >&2
  exit 1
fi
export DISPLAY=":$DISPLAY_NUM"

XEPHYR_PID=""
WM_PID=""

# The checker does not count a function the EXIT trap calls as used.
# shellcheck disable=SC2329
cleanup() {
  # lib.sh's helper: it stops every client spawn_client started. Called only
  # once lib.sh is sourced.
  if declare -F kill_clients >/dev/null; then
    kill_clients
  fi
  [ -n "$WM_PID" ] && kill -KILL "$WM_PID" 2>/dev/null
  [ -n "$XEPHYR_PID" ] && kill "$XEPHYR_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

# Two displays, and neither may be read from the same command that assigns it:
# Xephyr draws into the parent display and serves the nested one.
draw_into=$WM_PARENT_DISPLAY
serve_on=$DISPLAY
DISPLAY="$draw_into" Xephyr "$serve_on" -screen "${SCREEN_W}x${SCREEN_H}" -noreset \
  >"$XEPHYR_LOG" 2>&1 &
XEPHYR_PID=$!

for _ in $(seq 1 50); do
  xdotool getdisplaygeometry >/dev/null 2>&1 && break
  sleep 0.1
done
if ! xdotool getdisplaygeometry >/dev/null 2>&1; then
  echo "probe: Xephyr did not start on $DISPLAY" >&2
  cat "$XEPHYR_LOG" >&2
  exit 1
fi

# The WM reads a config file at startup. A probe answers "what does the WM do",
# and the answer must not change according to whose home directory it ran in, so
# a path that does not exist is the default. An explicit WM_CONFIG in the
# environment still wins, and the header names whichever one is in force.
export WM_CONFIG="${WM_CONFIG:-$LOG_DIR/no-config-file}"

printf 'probe: display=%s config=%s\n' "$DISPLAY" "$WM_CONFIG"

"$WM_VARIANT_BIN" >"$WM_LOG" 2>&1 &
WM_PID=$!
export WM_PID

# The WM says nothing when it is ready; the root window does. setup_xcb()
# selects SubstructureRedirect on it, and the key grabs are in place before the
# loop reads the first event, so a probe that answers here talks to a WM that
# answers keys. The margin after the mask covers the init between the two.
WM_READY=0
for _ in $(seq 1 200); do
  if ! kill -0 "$WM_PID" 2>/dev/null; then
    break
  fi
  if xwininfo -root -events 2>/dev/null | grep -q SubstructureRedirect; then
    sleep 0.2
    WM_READY=1
    break
  fi
  sleep 0.02
done

# lib.sh is the seam tests' own toolbox. It is sourced after the server is up
# because it insists on ROOT_DIR, and after the WM started because spawn_client
# waits for the windows it creates.
# shellcheck source=lib.sh
. "$ROOT_DIR/tests/x/lib.sh"

if [ "$WM_READY" -ne 1 ]; then
  if kill -0 "$WM_PID" 2>/dev/null; then
    echo "probe: the WM never selected SubstructureRedirect on $DISPLAY" >&2
  else
    echo "probe: the WM exited before it took the root window" >&2
  fi
  tail -n "$LOG_LINES" "$WM_LOG" >&2
  exit 1
fi

WINDOW_IDS=()
if [ "$N" -gt 0 ]; then
  mapfile -t WINDOW_IDS < <(spawn_client "$N" "$WIDTH" "$HEIGHT" probe)
fi

# sleep(1) takes a fractional argument in the dev shell's coreutils and in
# busybox; printing the value instead of dividing keeps the wait exact.
WAIT_SECONDS=$(printf '%d.%03d' $((WAIT_MS / 1000)) $((WAIT_MS % 1000)))
sleep "$WAIT_SECONDS"

STATUS=0
if [ -n "$COMMAND" ]; then
  printf -- '--- command: %s ---\n' "$COMMAND"
  bash -c "$COMMAND"
  STATUS=$?
  printf 'command: exit %s\n' "$STATUS"
  sleep "$WAIT_SECONDS"
fi

# One read of the viewable set, so a window cannot be reported viewable in one
# row and not in the next because a map landed between them.
mapfile -t VISIBLE < <(viewable_ids)
is_viewable() {
  local v want=$1
  for v in ${VISIBLE[@]+"${VISIBLE[@]}"}; do
    [ "$((v))" = "$((want))" ] && return 0
  done
  return 1
}

# prop_value <atom> <window-id|-root> - what xprop prints after the atom's name,
# and empty when the property is absent. xprop answers "not found." for a
# property nobody set, which is a value only by accident of parsing.
prop_value() {
  local value
  value=$(
    if [ "$2" = "root" ]; then
      xprop -root "$1" 2>/dev/null
    else
      xprop -id "$2" "$1" 2>/dev/null
    fi | sed -n '1s/^[^:]*:[[:space:]]*//p'
  )
  [ "$value" = "not found." ] && value=""
  printf '%s' "$value"
}

# tags_of <window-id> - the client's _NET_WM_DESKTOP, or '-' when the WM sets no
# such property.
tags_of() {
  local value
  value=$(prop_value _NET_WM_DESKTOP "$1")
  printf '%s' "${value:--}"
}

title_of() {
  local value
  value=$(xdotool getwindowname "$1" 2>/dev/null)
  printf '%s' "${value:--}"
}

printf -- '--- state ---\n'
printf 'screen: %s\n' "$(screen_size)"
printf 'window          x     y     w     h  border  viewable  focus  tags  title\n'
FOCUSED=$(focused_window)
for id in ${WINDOW_IDS[@]+"${WINDOW_IDS[@]}"}; do
  read -r wx wy ww wh <<<"$(rect_of "$id")"
  viewable=no
  is_viewable "$id" && viewable=yes
  focus=no
  if [ -n "$FOCUSED" ] && [ "$((FOCUSED))" = "$((id))" ]; then
    focus=yes
  fi
  printf '%-10s %6s %5s %5s %5s %7s %8s %6s %5s  %s\n' \
    "$id" "$wx" "$wy" "$ww" "$wh" "$(border_of "$id")" "$viewable" "$focus" \
    "$(tags_of "$id")" "$(title_of "$id")"
done
printf 'viewable: %d of %d\n' "$(count_viewable)" "$N"

printf -- '--- root properties ---\n'
for atom in _NET_SUPPORTING_WM_CHECK _NET_CLIENT_LIST _NET_CURRENT_DESKTOP \
  _NET_NUMBER_OF_DESKTOPS _NET_DESKTOP_NAMES; do
  value=$(prop_value "$atom" root)
  printf '%s: %s\n' "$atom" "${value:-not set}"
done

printf -- '--- wm ---\n'
WM_EXIT=""
if kill -0 "$WM_PID" 2>/dev/null; then
  printf 'wm: running (pid %s)\n' "$WM_PID"
else
  wait "$WM_PID" 2>/dev/null
  WM_EXIT=$?
  printf 'wm: dead (exit %s)\n' "$WM_EXIT"
fi

printf -- '--- wm log (last %s lines) ---\n' "$LOG_LINES"
tail -n "$LOG_LINES" "$WM_LOG"

if [ -n "$WM_EXIT" ]; then
  exit 1
fi
exit "$STATUS"
