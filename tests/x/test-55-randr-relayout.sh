#!/usr/bin/env bash
# A screen size change reaches the WM: after xrandr switches the nested output to
# a smaller mode, the visible clients are laid out again for the new screen.
#
# Xephyr exposes one output named "default" whose mode list is the standard VESA
# set, so a mode change is the one RandR configuration a nested server can be
# given. The server resizes the root window; whether the WM hears about it is
# what this test asks. Clients left at their old geometry are wider or taller
# than the screen, which is the assertion below.
source "$(dirname "$0")/lib.sh"

OUTPUT=""
ORIG_MODE=""
restore_mode() {
  if [ -n "$OUTPUT" ] && [ -n "$ORIG_MODE" ]; then
    xrandr --output "$OUTPUT" --mode "$ORIG_MODE" >/dev/null 2>&1
  fi
}
trap restore_mode EXIT

mapfile -t ids < <(spawn_client 2 300 200 randr)
require_visible 2 "both windows are viewable"

xorgh=$(xdotool getdisplaygeometry 2>/dev/null | awk '{print $1}')
OUTPUT=$(xrandr --query 2>/dev/null | awk '/ connected/{print $1; exit}')
ORIG_SETUP=$(xrandr --query 2>/dev/null | awk '/ connected/{print $3; exit}')
ORIG_MODE=${ORIG_SETUP%%+*}
if [ -z "$OUTPUT" ] || [ -z "$ORIG_MODE" ]; then
  fail "the nested server reports no connected RandR output, so no mode can be set"
  finish
  exit 1
fi

# The widest mode below the current width, so the clients that fit the old
# screen cannot fit the new one without a new layout.
TARGET=$(xrandr --query 2>/dev/null |
  awk '/^[[:space:]]+[0-9]+x[0-9]+/{print $1}' |
  sort -t x -k1,1n |
  awk -F x -v max="$xorgh" '$1 < max {target = $0} END {print target}')
if [ -z "$TARGET" ]; then
  fail "the nested server offers no mode narrower than ${xorgh}px, so nothing can be resized"
  finish
  exit 1
fi

xrandr --output "$OUTPUT" --mode "$TARGET" >/dev/null 2>&1
read -r screen_w screen_h <<<"$(screen_size)"
if [ "$screen_w" -ge "$xorgh" ]; then
  fail "xrandr did not resize the nested screen (still ${screen_w}x${screen_h}), so nothing tests the WM"
  finish
  exit 1
fi
pass "the nested screen is now ${screen_w}x${screen_h}"

# The WM answers the RandR event asynchronously; give it a moment to lay out.
waited=0
while [ "$waited" -lt 2000 ]; do
  inside=1
  for id in "${ids[@]}"; do
    read -r x y w h <<<"$(rect_of "$id")"
    border=$(border_of "$id")
    border=${border:-0}
    right=$((x + w + 2 * border))
    bottom=$((y + h + 2 * border))
    if [ "$x" -lt 0 ] || [ "$right" -gt "$screen_w" ] || [ "$y" -lt 0 ] || [ "$bottom" -gt "$screen_h" ]; then
      inside=0
    fi
  done
  [ "$inside" -eq 1 ] && break
  sleep 0.02
  waited=$((waited + 20))
done

outside=""
area=0
for id in "${ids[@]}"; do
  read -r x y w h <<<"$(rect_of "$id")"
  border=$(border_of "$id")
  border=${border:-0}
  right=$((x + w + 2 * border))
  bottom=$((y + h + 2 * border))
  area=$((area + w * h))
  if [ "$x" -lt 0 ] || [ "$right" -gt "$screen_w" ] || [ "$y" -lt 0 ] || [ "$bottom" -gt "$screen_h" ]; then
    outside="$outside ${id} at ${x},${y} ${w}x${h}"
  fi
done

if [ -z "$outside" ]; then
  pass "the WM re-laid the clients out for the new screen size"
else
  fail "the WM re-laid the clients out for the new screen size:${outside} still extends past it"
fi
check_ge "the clients cover at least half of the new screen (px)" "$((screen_w * screen_h / 2))" "$area"
check_eq "no overlapping window pairs after the mode change" "0" "$(overlap_pairs "${ids[@]}")"
check_wm_alive

finish
