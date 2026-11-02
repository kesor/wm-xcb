#!/usr/bin/env bash
# A client that names a minimum size larger than the area the layout gives it
# keeps that size. The hint is read from WM_NORMAL_HINTS, and the layout runs
# again once the reply arrives, so this also asserts that the second layout
# reaches X. xclient writes the property the way Xlib's XSetWMNormalHints()
# does: 18 slots with every field at its own offset, position and size zero
# because no flag names them, and the minimum at slot 5. A reader that walks
# the flags finds the zeros instead.
source "$(dirname "$0")/lib.sh"

MIN_W=1400
MIN_H=900

mapfile -t ids < <(spawn_client 1 400 300 hinted -M "${MIN_W}x${MIN_H}")
check_eq "one window created" "1" "${#ids[@]}"

id=${ids[0]}
require_visible 1 "the window is viewable (its hints can be observed)"

# The hint arrives after the first layout, so wait rather than read once.
waiting=0
while [ "$waiting" -lt 2000 ]; do
  read -r _ _ w h <<<"$(rect_of "$id")"
  [ "$w" -ge "$MIN_W" ] && [ "$h" -ge "$MIN_H" ] && break
  sleep 0.02
  waiting=$((waiting + 20))
done

read -r _ _ w h <<<"$(rect_of "$id")"
check_ge "the window keeps the minimum width it asked for (px)" "$MIN_W" "$w"
check_ge "the window keeps the minimum height it asked for (px)" "$MIN_H" "$h"

finish
