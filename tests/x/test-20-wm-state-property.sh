#!/usr/bin/env bash
# A managed window carries WM_STATE=NormalProperty, which is how other clients
# tell a managed window from an unmanaged one.
source "$(dirname "$0")/lib.sh"

id=$(spawn_client 1 400 300 wmstate)
wait_viewable 1 2000 >/dev/null

if [ -z "$id" ]; then
  fail "no window to inspect"
  finish
  exit $?
fi

state=$(xprop -id "$id" WM_STATE 2>/dev/null)
case "$state" in
  *Normal*) pass "WM_STATE is Normal on the managed window" ;;
  *) fail "WM_STATE is not set to Normal: '$state' (STATUS 4.1)" ;;
esac

finish
