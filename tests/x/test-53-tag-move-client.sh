#!/usr/bin/env bash
# Moving the focused client to another tag takes it out of the visible set.
#
# The binding the default config wires is MOD+Shift+Alt+N, and the Alt in that
# mask is Mod5 in the X modifier map, which is the key labelled AltGr. The
# nested server's keymap carries it as ISO_Level3_Shift, so that is the modifier
# xdotool has to press: plain "alt" is Mod1 and matches no grab.
source "$(dirname "$0")/lib.sh"

TARGET_TAG=2

press super+1
sleep 0.3
mapfile -t ids < <(spawn_client 1 400 300 tagnmove)
check_eq "one window created" "1" "${#ids[@]}"
require_visible 1 "the client is visible on tag 1"

# A keymap without a Mod5 key cannot press this binding at all, and the check
# below would then fail for a reason that is not the WM's.
press_out=$(xdotool key --clearmodifiers "super+shift+ISO_Level3_Shift+$TARGET_TAG" 2>&1)
case "$press_out" in
*"No such key name"*)
  fail "the nested keymap has no Mod5 key, so MOD+Shift+Alt+$TARGET_TAG cannot be pressed: $press_out"
  press super+1
  finish
  exit 1
  ;;
esac

sleep 0.5
check_eq "the moved client left the visible tag set" "0" "$(count_viewable)"

# Still managed on the tag it moved to: switching there shows it again. Without
# this, a client the move had dropped would pass the check above.
press "super+$TARGET_TAG"
sleep 0.4
check_eq "the moved client is visible on tag $TARGET_TAG" "1" "$(count_viewable)"

press super+1
sleep 0.2

check_wm_alive
finish
