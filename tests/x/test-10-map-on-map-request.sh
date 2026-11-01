#!/usr/bin/env bash
# A window a client maps must end up viewable. The WM holds substructure
# redirect, so nothing else can map it.
source "$(dirname "$0")/lib.sh"

echo "  scenario: one client maps a window"
id=$(spawn_client 1 400 300 mapped)
check_nonempty "client created a window" "$id"

if wait_viewable 1 2000; then
  pass "window is viewable"
else
  fail "window never became viewable: the WM does not map windows on MapRequest (STATUS 4.1)"
fi

finish
