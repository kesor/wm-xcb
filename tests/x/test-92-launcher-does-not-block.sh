#!/usr/bin/env bash
# MOD+p must not stop the WM from handling keys while dmenu is open.
#
# run.sh puts a fake dmenu on PATH that writes nothing and stays open. That is
# what the WM sees while a user looks at the prompt: a child holding a pipe
# open. How long MOD+2 takes to hide the tag-1 windows is the assertion. A
# launcher that reads the pipe and waits for the child never returns to the
# loop, so the tag view does not change until dmenu exits. The real dmenu grabs
# the keyboard for as long as it runs, so this key delivery can only be measured
# against the fake; what the fake shows is that the WM's loop is free.
source "$(dirname "$0")/lib.sh"

spawn_client 1 400 300 tagged >/dev/null
wait_viewable 1 2000 >/dev/null
require_visible 1 "window visible before the launcher"

now_ms() {
  echo $(($(date +%s%N) / 1000000))
}

# The fixture has to carry every name a launcher may exec, or a launcher that
# does block still stops the test below as "dmenu did not start", which is a
# different defect and says nothing about the loop.
check_nonempty "the suite fakes dmenu_run as well as dmenu" "$(command -v dmenu_run 2>/dev/null)"

# leave the WM alone until dmenu has really started: a launcher that never ran
# would make the assertion below pass because the WM was idle, not responsive.
rm -f "$WM_SEAM_DMENU_STARTED"
press super+p
for _ in $(seq 1 100); do
  [ -e "$WM_SEAM_DMENU_STARTED" ] && break
  sleep 0.02
done
if [ ! -e "$WM_SEAM_DMENU_STARTED" ]; then
  fail "MOD+p did not start dmenu, so nothing is holding the loop (STATUS 4.5)"
  finish
  exit 1
fi
pass "dmenu is open and its pipe is still waiting for a selection"

# The press is repeated inside the budget so a lost synthetic key event cannot
# be read as a blocked loop. Pressing the tag the view is already on does
# nothing, so a repeat costs one request and changes no state.
KEYS_MS=0
keys_start=$(now_ms)
keys_deadline=$((keys_start + 500))
while [ "$(now_ms)" -lt "$keys_deadline" ]; do
  if [ "$(count_viewable)" -eq 0 ]; then
    KEYS_MS=$(( $(now_ms) - keys_start ))
    break
  fi
  press super+2
  sleep 0.02
done

if [ "$KEYS_MS" -gt 0 ]; then
  pass "MOD+2 hid the tag-1 window in ${KEYS_MS} ms while dmenu was open"
else
  fail "MOD+2 did not reach the WM within 500 ms of MOD+p: the launcher blocks the loop"
fi

press super+1
for _ in $(seq 1 50); do
  [ "$(count_viewable)" -eq 1 ] && break
  sleep 0.02
done
check_eq "MOD+1 shows the tag-1 window again" "1" "$(count_viewable)"

check_wm_alive

finish
