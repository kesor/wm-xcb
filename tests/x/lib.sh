#!/usr/bin/env bash
#
# lib.sh - helpers for the seam tests in tests/x/.
#
# A seam test asserts something about the X server, not about the WM's internal
# structures: a window is viewable, a geometry is what tiling promised, the
# focus is where the WM said it would be, a root property exists. Those are the
# things a user sees, and they are the part the unit tests cannot reach.
#
# Sourced by each tests/x/test-*.sh. run.sh has already started Xephyr and the
# WM and exported DISPLAY, WM_LOG, WM_PID and ROOT_DIR.
#
# A test prints one line per assertion and exits 0 only if every assertion
# passed.

: "${ROOT_DIR:?run the seam tests through tests/x/run.sh}"

# run.sh unsets DEBUG before it runs a test, because libxdo prints its whole
# keymap to stderr whenever that variable is set to any value. It passes the
# variant as WM_SEAM_DEBUG for the tests that start a server of their own. This
# is the second line of defence for a test run by hand:
#   DEBUG=1 tests/x/test-50-tag-switch-keybinding.sh
WM_SEAM_DEBUG=${WM_SEAM_DEBUG:-${DEBUG:-0}}
export WM_SEAM_DEBUG
unset DEBUG

XCLIENT="$ROOT_DIR/tools/xclient"
PASS_COUNT=0
FAIL_COUNT=0

# Unique per test run so two tests never share a WM_CLASS.
CLASS="seam-$$-$RANDOM"

pass() {
  PASS_COUNT=$((PASS_COUNT + 1))
  printf '  ok    %s\n' "$*"
}

fail() {
  FAIL_COUNT=$((FAIL_COUNT + 1))
  printf '  FAIL  %s\n' "$*"
}

check_eq() { # check_eq <what> <expected> <actual>
  if [ "$2" = "$3" ]; then
    pass "$1 ($3)"
  else
    fail "$1: expected '$2', got '$3'"
  fi
}

check_ge() { # check_ge <what> <minimum> <actual>
  if [ "$3" -ge "$2" ] 2>/dev/null; then
    pass "$1 ($3 >= $2)"
  else
    fail "$1: expected at least '$2', got '$3'"
  fi
}

check_le() { # check_le <what> <maximum> <actual>
  if [ "$3" -le "$2" ] 2>/dev/null; then
    pass "$1 ($3 <= $2)"
  else
    fail "$1: expected at most '$2', got '$3'"
  fi
}

check_nonempty() { # check_nonempty <what> <value>
  if [ -n "$2" ]; then
    pass "$1 ($2)"
  else
    fail "$1: empty"
  fi
}

check_wm_alive() {
  if kill -0 "$WM_PID" 2>/dev/null; then
    pass "WM still running"
  else
    fail "WM died; last log lines:"
    tail -n 15 "$WM_LOG" 2>/dev/null | sed 's/^/        /'
  fi
}

# spawn_client [count] [width] [height] [title-prefix] [extra xclient args...]
# Starts tools/xclient and prints its window ids, one per line, on stdout.
# The extra arguments are passed to xclient as they are, for the tests that need
# it to publish something other than the defaults.
spawn_client() {
  local count=${1:-1} width=${2:-400} height=${3:-300} title=${4:-seam}
  local -a extra=()
  if [ "$#" -gt 4 ]; then
    shift 4
    extra=("$@")
  fi
  local out="$TMPDIR_SEAM/client-$count-$$-$RANDOM.txt"
  "$XCLIENT" -n "$count" -W "$width" -H "$height" -t "$title" -c "$CLASS" -v "${extra[@]}" \
    >"$out" 2>&1 &
  # This function runs inside a command substitution (mapfile, $(...)), so a
  # shell variable would be set in a subshell and lost. The pid goes to a file
  # that kill_clients reads; $$ is the test process, the same in both calls.
  echo "$!" >>"$TMPDIR_SEAM/client-pids.$$" 
  # The client prints "0x<id> <title>" per window once each is created.
  local waited=0
  while [ "$(wc -l <"$out")" -lt "$count" ] && [ "$waited" -lt 200 ]; do
    sleep 0.01
    waited=$((waited + 1))
  done
  awk '{print $1}' "$out"
}

# viewable_ids - window ids that the X server reports as viewable, for this
# test's own client class only.
viewable_ids() {
  xdotool search --onlyvisible --class "$CLASS" 2>/dev/null
}

all_ids() {
  xdotool search --class "$CLASS" 2>/dev/null
}

count_viewable() {
  viewable_ids | grep -c . || true
}

# wait_viewable <count> [timeout_ms] - returns 0 when at least <count> of this
# test's windows are viewable.
wait_viewable() {
  local want=$1 timeout=${2:-2000} waited=0
  while [ "$waited" -lt "$timeout" ]; do
    if [ "$(count_viewable)" -ge "$want" ]; then
      return 0
    fi
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

# require_visible <count> <what> - stop the test unless <count> of this test's
# windows are viewable. A later assertion that "the window is gone" or "the
# window is hidden" would otherwise pass for the wrong reason.
require_visible() {
  local want=$1 what=$2
  if wait_viewable "$want" 2000; then
    pass "$what"
  else
    fail "$what: only $(count_viewable) of $want windows became viewable (STATUS 4.1)"
    finish
    exit 1
  fi
}

# rect_of <window-id> - prints "x y w h"
rect_of() {
  xdotool getwindowgeometry --shell "$1" 2>/dev/null |
    awk -F= '/^X=/{x=$2}/^Y=/{y=$2}/^WIDTH=/{w=$2}/^HEIGHT=/{h=$2}END{print x+0, y+0, w+0, h+0}'
}

screen_size() {
  xdotool getdisplaygeometry 2>/dev/null | awk '{print $1, $2}'
}

# border_of <window-id> - the border width X reports for the window. xdotool
# reports a window's inside size and its outer corner, so the border is the only
# part of its footprint it does not print.
border_of() {
  xwininfo -id "$1" 2>/dev/null | awk -F: '/Border width/{print $2+0; exit}'
}

# overlap_pairs <id>... - prints the number of overlapping window pairs
overlap_pairs() {
  local ids=("$@") i j count=0
  for i in "${!ids[@]}"; do
    local ax ay aw ah
    read -r ax ay aw ah <<<"$(rect_of "${ids[$i]}")"
    for j in "${!ids[@]}"; do
      [ "$j" -le "$i" ] && continue
      local bx by bw bh
      read -r bx by bw bh <<<"$(rect_of "${ids[$j]}")"
      if [ "$ax" -lt $((bx + bw)) ] && [ "$bx" -lt $((ax + aw)) ] &&
        [ "$ay" -lt $((by + bh)) ] && [ "$by" -lt $((ay + ah)) ]; then
        count=$((count + 1))
      fi
    done
  done
  echo "$count"
}

# focused_window - the window the X server currently has as input focus.
focused_window() {
  xdotool getwindowfocus 2>/dev/null
}

# wait_focus <window-id> [timeout_ms] - returns 0 once the X input focus is that
# window. The WM answers an enter event asynchronously, so a test that reads the
# focus right after moving the pointer races the WM.
wait_focus() {
  local want=$1 timeout=${2:-1000} waited=0 got
  while [ "$waited" -lt "$timeout" ]; do
    got=$(focused_window)
    [ -n "$got" ] && [ "$((got))" = "$((want))" ] && return 0
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

# last_client_pid - the process id behind the most recent spawn_client call.
# spawn_client appends one pid per call, and a call with count 1 owns exactly
# one window, so this is the client of the last window it printed.
last_client_pid() {
  tail -n 1 "$TMPDIR_SEAM/client-pids.$$" 2>/dev/null
}

# press <key> - send a key to the nested display through xdotool.
press() {
  xdotool key --clearmodifiers "$1" 2>/dev/null
}

root_prop() { # root_prop <atom>
  xprop -root "$1" 2>/dev/null
}

# kill_clients - stop every client this test started.
kill_clients() {
  local pid pids="$TMPDIR_SEAM/client-pids.$$"
  [ -f "$pids" ] || return 0
  while IFS= read -r pid; do
    [ -n "$pid" ] && kill "$pid" 2>/dev/null
  done <"$pids"
  rm -f "$pids"
}

finish() {
  kill_clients
  printf '  %d passed, %d failed\n' "$PASS_COUNT" "$FAIL_COUNT"
  [ "$FAIL_COUNT" -eq 0 ]
}
