#!/usr/bin/env bash
# Two windows must be tiled: no overlap, and together they fill most of the
# screen. This is the assertion that the tiling component reached X.
source "$(dirname "$0")/lib.sh"

mapfile -t ids < <(spawn_client 2 400 300 tiled)
check_eq "two windows created" "2" "${#ids[@]}"

require_visible 2 "both windows are viewable (tiling can be observed)"

check_eq "no overlapping window pairs" "0" "$(overlap_pairs "${ids[@]}")"

read -r sw sh <<<"$(screen_size)"
covered=0
for id in "${ids[@]}"; do
  read -r _ _ w h <<<"$(rect_of "$id")"
  covered=$((covered + w * h))
done
half=$((sw * sh / 2))
check_ge "windows cover at least half the screen (px)" "$half" "$covered"

for id in "${ids[@]}"; do
  read -r _ _ w h <<<"$(rect_of "$id")"
  check_ge "window $id has a real size (px²)" "1000" "$((w * h))"
done

# A window's border is drawn inside its bounding box, so a client that is given
# an area has to be configured with that area minus a border on each side. What
# that buys is visible here: the two windows meet exactly, with no gap between
# them and no overlap of the borders, and the pair reaches both screen edges.
read -r ax ay aw ah <<<"$(rect_of "${ids[0]}")"
read -r bx by bw bh <<<"$(rect_of "${ids[1]}")"
# The newest client is the master, and the master is the left column; which id
# that is does not matter here, so order the two by position instead.
if [ "$ax" -le "$bx" ]; then
  left_x=$ax
  left_y=$ay
  left_w=$aw
  left_h=$ah
  right_x=$bx
  right_w=$bw
else
  left_x=$bx
  left_y=$by
  left_w=$bw
  left_h=$bh
  right_x=$ax
  right_w=$aw
fi
border=$(border_of "${ids[0]}")
check_ge "the WM gave the window a border (px)" "1" "$border"
check_eq "the left window starts at the screen edge" "0" "$left_x"
check_eq "the right window starts where the left one's border ends" \
  "$((left_x + left_w + 2 * border))" "$right_x"
check_eq "the pair fills the screen width, borders included" \
  "$sw" "$((right_x + right_w + 2 * border))"
check_eq "the pair fills the screen height, borders included" \
  "$sh" "$((left_y + left_h + 2 * border))"

finish
