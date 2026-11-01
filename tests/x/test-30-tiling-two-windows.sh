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

finish
