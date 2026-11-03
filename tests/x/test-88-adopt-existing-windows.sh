#!/usr/bin/env bash
#
# test-88-adopt-existing-windows.sh - windows that were on the display before
# the WM started are taken into the client model.
#
# Such a window never sends a MapRequest: the server mapped it while no WM was
# running. Without a startup scan it stays visible and untracked, so it is not
# tiled, carries no WM_STATE, and is not in the client list. The check has to
# happen before the WM starts, so this test starts its own Xvfb and WM rather
# than using the suite's already-started one.
#
# Assertions:
#   - both pre-existing windows are viewable and do not overlap (they were
#     tiled, which only happens to clients)
#   - both carry WM_STATE=Normal, which the manage path writes and nothing else
#   - an override-redirect window keeps the geometry its client chose and is
#     left without WM_STATE

set -uo pipefail

source "$(dirname "$0")/lib.sh"

if ! command -v Xvfb >/dev/null 2>&1; then
  fail "Xvfb is available to start a private display"
  finish
  exit 1
fi

VARIANT=$(make -s -C "$ROOT_DIR" print-variant DEBUG="${WM_SEAM_DEBUG:-0}" SANITIZE="${SANITIZE:-0}")
WM_BIN="$ROOT_DIR/build/$VARIANT/wm"
if [ ! -x "$WM_BIN" ]; then
  fail "the WM under test exists ($WM_BIN)"
  finish
  exit 1
fi

DIR="$TMPDIR_SEAM/adopt-existing"
rm -rf "$DIR"
mkdir -p "$DIR"
WM_LOG="$DIR/wm.log"
XVFB_LOG="$DIR/xvfb.log"
OVERRIDE_OUT="$DIR/override.out"

DISPLAY_NUM=""
for n in 30 31 32 33 34 35 36 37 38 39; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
if [ -z "$DISPLAY_NUM" ]; then
  fail "a free display in :30..:39"
  finish
  exit 1
fi

ADOPT_DISPLAY=":$DISPLAY_NUM"
OVERRIDE_CLASS="$CLASS-override"
XVFB_PID=""
AWM_PID=""
OVERRIDE_PID=""

cleanup() {
  [ -n "$OVERRIDE_PID" ] && kill "$OVERRIDE_PID" 2>/dev/null
  [ -n "$AWM_PID" ] && kill "$AWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$ADOPT_DISPLAY" -screen 0 1280x800x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

up=0
for _ in $(seq 1 100); do
  if DISPLAY="$ADOPT_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    up=1
    break
  fi
  sleep 0.05
done
if [ "$up" -ne 1 ]; then
  fail "a private display is up ($ADOPT_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($ADOPT_DISPLAY)"

# spawn_client and the check_* helpers work on whatever DISPLAY the test has.
export DISPLAY="$ADOPT_DISPLAY"

mapfile -t ids < <(spawn_client 2 400 300 adopt)
check_eq "two windows created before the WM started" "2" "${#ids[@]}"
require_visible 2 "both windows are viewable before the WM starts"

check_eq "the windows overlap while no WM runs" "1" "$(overlap_pairs "${ids[@]}")"

DISPLAY="$ADOPT_DISPLAY" "$XCLIENT" -n 1 -W 100 -H 100 -t override -c "$OVERRIDE_CLASS" \
  -o -y -v >"$OVERRIDE_OUT" 2>&1 &
OVERRIDE_PID=$!

override_id=""
for _ in $(seq 1 200); do
  override_id=$(awk 'NR == 1 { print $1 }' "$OVERRIDE_OUT" 2>/dev/null)
  [ -n "$override_id" ] && break
  sleep 0.01
done
if [ -z "$override_id" ]; then
  fail "the override-redirect window was created"
  finish
  exit 1
fi
pass "the override-redirect window was created ($override_id)"

DISPLAY="$ADOPT_DISPLAY" "$WM_BIN" >"$WM_LOG" 2>&1 &
AWM_PID=$!
sleep 1

if ! kill -0 "$AWM_PID" 2>/dev/null; then
  fail "the WM is running after startup"
  tail -n 15 "$WM_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "the WM is running after startup"

require_visible 2 "the pre-existing windows are still viewable"

check_eq "no overlapping window pairs" "0" "$(overlap_pairs "${ids[@]}")"

read -r sw sh <<<"$(screen_size)"
covered=0
for id in "${ids[@]}"; do
  read -r _ _ w h <<<"$(rect_of "$id")"
  covered=$((covered + w * h))
done
check_ge "the adopted windows cover at least half the screen (px)" "$((sw * sh / 2))" "$covered"

for id in "${ids[@]}"; do
  state=$(xprop -id "$id" WM_STATE 2>/dev/null)
  case "$state" in
    *Normal*) pass "WM_STATE is Normal on adopted window $id" ;;
    *) fail "WM_STATE is Normal on adopted window $id: '$state'" ;;
  esac
done

read -r _ _ ow oh <<<"$(rect_of "$override_id")"
check_eq "the override-redirect window keeps its width" "100" "$ow"
check_eq "the override-redirect window keeps its height" "100" "$oh"

case "$(xprop -id "$override_id" WM_STATE 2>/dev/null)" in
  *Normal*) fail "the override-redirect window is left without WM_STATE" ;;
  *) pass "the override-redirect window is left without WM_STATE" ;;
esac

if kill -0 "$AWM_PID" 2>/dev/null; then
  pass "WM still running"
else
  fail "WM died; last log lines:"
  tail -n 15 "$WM_LOG" 2>/dev/null | sed 's/^/        /'
fi

finish
