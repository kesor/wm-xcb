#!/usr/bin/env bash
# MOD+Shift+space must take the focused window out of the tiled layout and put
# it back. A floating window keeps the rectangle it has: the layout stops
# placing it, so a tag round trip - which re-tiles the monitor twice - leaves
# its size alone, and tiling it again gives it a slot beside the other window.
source "$(dirname "$0")/lib.sh"

# outer_rect <window-id> - prints "x y w h" with the border added, the
# footprint the user sees.
outer_rect() {
  local id=$1 x y w h border
  read -r x y w h <<<"$(rect_of "$id")"
  border=$(border_of "$id")
  printf '%s %s %s %s\n' "$x" "$y" "$((w + 2 * border))" "$((h + 2 * border))"
}

# above <window-id> <other> - yes when <window-id> is stacked above <other>.
# xwininfo -root -tree prints the root's children top-most first: it walks the
# XQueryTree list from the last index, and that list is bottom-most first. A
# geometry check cannot see this. The window left in the layout is given the
# whole monitor, so a floating window under it is covered and still reports
# the rectangle it had.
above() {
  local want other want_hex other_hex seen
  want=$1
  other=$2
  want_hex=$(printf '0x%x' "$want")
  other_hex=$(printf '0x%x' "$other")
  seen=$(xwininfo -root -tree | awk -v w="$want_hex" -v o="$other_hex" '
    $1 == w { print "want"; exit }
    $1 == o { print "other"; exit }
  ')
  if [ "$seen" = "want" ]; then
    echo yes
  else
    echo no
  fi
}

mapfile -t ids < <(spawn_client 2 400 300 floating)
check_eq "two windows created" "2" "${#ids[@]}"

require_visible 2 "both windows are viewable before the key"

focused=$(focused_window)
if ! all_ids | grep -qx "$focused"; then
  fail "the X input focus is not on either of this test's windows (got '$focused'); the key would reach another client"
  finish
  exit $?
fi
other=$(all_ids | grep -vx "$focused" | head -n 1)
pass "the focused window is one of this test's clients"

read -r sw sh <<<"$(screen_size)"

before=$(outer_rect "$focused")
check_eq "the focused window starts in a tiled slot, not over the monitor" "no" \
  "$([ "$before" = "0 0 $sw $sh" ] && echo yes || echo no)"

press super+shift+space
sleep 0.3

# Nothing moves the window on the way out. The rest of the layout reflows,
# because the floating window no longer takes its share of the monitor.
check_eq "MOD+Shift+space leaves the floating window where it was" "$before" \
  "$(outer_rect "$focused")"
check_eq "the window left in the layout covers the monitor" "0 0 $sw $sh" \
  "$(outer_rect "$other")"
check_eq "the floating window is above the window that took the monitor" "yes" \
  "$(above "$focused" "$other")"

press super+shift+space
sleep 0.3

check_eq "a second MOD+Shift+space tiles the two windows without overlap" "0" \
  "$(overlap_pairs "$focused" "$other")"

read -r ax _ aw _ <<<"$(outer_rect "$focused")"
read -r bx _ bw _ <<<"$(outer_rect "$other")"
if [ "$ax" -le "$bx" ]; then
  left_x=$ax
  right_x=$bx
  right_w=$bw
else
  left_x=$bx
  right_x=$ax
  right_w=$aw
fi
check_eq "the pair reaches the screen edges again" "0 $sw" "$left_x $((right_x + right_w))"
check_le "the window that came back shares the monitor again" \
  "$((sw - 1))" "$aw"

# The same window goes out again, and a tag round trip re-tiles the monitor
# twice with it out of the layout.
back=$(outer_rect "$focused")
press super+shift+space
sleep 0.3
check_eq "the second float leaves the window in the slot it was given" "$back" \
  "$(outer_rect "$focused")"

press super+2
sleep 0.4
check_eq "MOD+2 hid both windows" "0" "$(count_viewable)"

press super+1
sleep 0.4
check_eq "MOD+1 shows both windows again" "2" "$(count_viewable)"
check_eq "the floating window keeps its size across the tag switch" "$back" \
  "$(outer_rect "$focused")"
check_eq "the floating window is still above the layout after the tag switch" "yes" \
  "$(above "$focused" "$other")"

finish
