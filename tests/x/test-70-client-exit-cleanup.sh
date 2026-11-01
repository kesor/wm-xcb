#!/usr/bin/env bash
# A client that exits must not take the WM with it, and its window must stop
# being viewable.
source "$(dirname "$0")/lib.sh"

pid_and_ids=$(spawn_client 1 400 300 shortlived)
require_visible 1 "window visible before the client exits"

kill_clients
sleep 0.5

check_wm_alive
check_eq "window is no longer viewable" "0" "$(count_viewable)"

finish
