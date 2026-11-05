#!/usr/bin/env bash
# A panel or paginator finds the WM through _NET_SUPPORTING_WM_CHECK, lists the
# windows through _NET_CLIENT_LIST, and switches tags by asking for another
# desktop through _NET_CURRENT_DESKTOP. This is the whole loop an external
# desktop tool needs, against a live WM.
source "$(dirname "$0")/lib.sh"

# property_value - the value half of the line xprop prints. A CARDINAL prints
# as "name = 0" and a WINDOW as "name: window id # 0x400000", so both shapes
# have to come out as the value alone.
property_value() {
  sed -e 's/^[^=:]*[=:][[:space:]]*//' -e 's/^window id # //'
}

# root_value <atom> - what a root property holds, without the property name.
# Without -notype the answer carries the property's type and width as well.
root_value() {
  xprop -root -notype "$1" 2>/dev/null | property_value
}

# window_value <window-id> <atom> - the same, for a property on a window.
window_value() {
  xprop -id "$1" -notype "$2" 2>/dev/null | property_value
}

# client_list - the windows _NET_CLIENT_LIST holds, one decimal id per line.
client_list() {
  local id
  for id in $(root_value _NET_CLIENT_LIST | tr -d ','); do
    printf '%d\n' "$id"
  done
}

# wait_client_list <count> - the property is written while the WM handles the
# map, so a read right after the window appears races the flush.
wait_client_list() {
  local want=$1 waited=0 got
  while [ "$waited" -lt 2000 ]; do
    got=$(client_list | grep -c . || true)
    [ "$got" = "$want" ] && return 0
    sleep 0.02
    waited=$((waited + 20))
  done
  return 1
}

mapfile -t ids < <(spawn_client 2 400 300 ewmh)
require_visible 2 "both clients are on screen before the property checks"

first=$(printf '%d' "${ids[0]}")
second=$(printf '%d' "${ids[1]}")

check_nonempty "_NET_SUPPORTING_WM_CHECK is set" "$(root_value _NET_SUPPORTING_WM_CHECK)"

check_win=$(root_value _NET_SUPPORTING_WM_CHECK)
check_eq "the check window points back at itself" \
  "$check_win" "$(window_value "$check_win" _NET_SUPPORTING_WM_CHECK)"
check_eq "the check window names the WM" "\"wm-xcb\"" \
  "$(window_value "$check_win" _NET_WM_NAME)"

check_eq "_NET_NUMBER_OF_DESKTOPS counts the tags" "9" \
  "$(root_value _NET_NUMBER_OF_DESKTOPS)"
check_nonempty "_NET_DESKTOP_NAMES names the first desktop" \
  "$(root_value _NET_DESKTOP_NAMES | tr -d '",' | awk '{print $1}' | grep -x '1')"
check_eq "_NET_CURRENT_DESKTOP starts on the first tag" "0" \
  "$(root_value _NET_CURRENT_DESKTOP)"

if wait_client_list 2; then
  pass "_NET_CLIENT_LIST holds both managed windows"
else
  fail "_NET_CLIENT_LIST holds $(client_list | grep -c . || true) of 2 windows: $(root_value _NET_CLIENT_LIST)"
fi

# EWMH asks for the order the windows were mapped in, and these two were mapped
# one after the other.
check_eq "_NET_CLIENT_LIST is in mapping order" "$first $second" \
  "$(client_list | tr '\n' ' ' | sed 's/ $//')"

check_eq "_NET_WM_DESKTOP puts the first client on its tag" "0" \
  "$(window_value "$first" _NET_WM_DESKTOP)"
check_eq "_NET_WM_DESKTOP puts the second client on its tag" "0" \
  "$(window_value "$second" _NET_WM_DESKTOP)"

# A pager asks for desktop 1; the WM must switch the tag view, which the panel
# reads back as _NET_CURRENT_DESKTOP and the user sees as the clients leaving.
xdotool set_desktop 1 2>/dev/null
switched=0
for _ in $(seq 1 100); do
  if [ "$(root_value _NET_CURRENT_DESKTOP)" = "1" ]; then
    switched=1
    break
  fi
  sleep 0.02
done
if [ "$switched" = "1" ]; then
  pass "a _NET_CURRENT_DESKTOP request switched the desktop"
else
  fail "a _NET_CURRENT_DESKTOP request left the desktop at $(root_value _NET_CURRENT_DESKTOP)"
fi

hidden=0
for _ in $(seq 1 100); do
  if [ "$(count_viewable)" = "0" ]; then
    hidden=1
    break
  fi
  sleep 0.02
done
if [ "$hidden" = "1" ]; then
  pass "the tag the desktop switched away from is off screen"
else
  fail "a client is still viewable after the desktop switch"
fi

# A client that is off screen is still managed, and EWMH has no "not on screen
# now": the window stays in the list a panel builds its task bar from.
check_eq "a hidden client stays in _NET_CLIENT_LIST" "2" "$(client_list | grep -c . || true)"

xdotool set_desktop 0 2>/dev/null
if wait_viewable 2 2000; then
  pass "switching back to desktop 0 shows the clients again"
else
  fail "the clients did not come back after switching to desktop 0"
fi

finish
