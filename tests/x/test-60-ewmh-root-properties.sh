#!/usr/bin/env bash
# A panel or paginator finds the WM through _NET_SUPPORTING_WM_CHECK, and lists
# windows through _NET_CLIENT_LIST.
source "$(dirname "$0")/lib.sh"

spawn_client 1 400 300 ewmh >/dev/null
wait_viewable 1 2000 >/dev/null

check_nonempty "_NET_SUPPORTING_WM_CHECK is set" \
  "$(root_prop _NET_SUPPORTING_WM_CHECK | grep -v 'not found')"
check_nonempty "_NET_CLIENT_LIST is set" \
  "$(root_prop _NET_CLIENT_LIST | grep -v 'not found')"

finish
