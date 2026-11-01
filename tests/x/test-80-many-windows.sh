#!/usr/bin/env bash
# Twenty windows at once: all viewable, none overlapping, WM still alive. This
# is the seam that catches a manage path that works for one window and leaks or
# drops events when they arrive in a burst.
source "$(dirname "$0")/lib.sh"

N=20
mapfile -t ids < <(spawn_client "$N" 200 150 burst)
require_visible "$N" "all $N windows are viewable"

check_eq "no overlapping window pairs" "0" "$(overlap_pairs "${ids[@]}")"
check_wm_alive
finish
