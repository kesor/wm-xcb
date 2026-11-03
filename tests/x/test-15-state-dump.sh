#!/usr/bin/env bash
#
# test-15-state-dump.sh - SIGUSR1 prints every live state machine.
#
# The promise behind the state machines is that "why is this window fullscreen"
# can be answered from one machine. That needs a way to ask a running WM, and
# `kill -USR1 <pid>` is it: the loop prints every live machine's owner, state,
# entrance counts and last changes, and keeps running.
#
# What is asserted is the text a person reads: the machines the startup path
# creates are in it, each with a state and a trace.
#
# run.sh has already started the WM and exported WM_PID, WM_LOG and ROOT_DIR.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

# A managed client gives the dump the client machines, and their traces.
WINDOW=$(spawn_client 1 240 180 | head -n1)
if [ -z "$WINDOW" ]; then
  fail "a client to own a state machine"
  finish
  exit 1
fi
sleep 0.3

offset=$(wc -c <"$WM_LOG")

kill -USR1 "$WM_PID"

# The signal handler only sets a flag; the dump is written by the event loop, so
# it arrives on the loop's next pass. Poll the log instead of sleeping a fixed
# time, which would pass on a fast machine and fail on a loaded one.
dump=""
for _ in $(seq 1 50); do
  dump=$(tail -c +$((offset + 1)) "$WM_LOG")
  case "$dump" in
    *"state machines: "*) break ;;
  esac
  sleep 0.1
done

check_nonempty "a dump was written" "$(printf '%s\n' "$dump" | grep -m1 '^state machines: ')"
check_nonempty "the dump counts the live machines" "$(printf '%s\n' "$dump" | grep -m1 '^state machines: [0-9]')"
check_nonempty "the dump names the client that owns the client machines" "$(printf '%s\n' "$dump" | grep -m1 '^client ')"
check_nonempty "the dump names the monitor machines" "$(printf '%s\n' "$dump" | grep -m1 '^monitor ')"
check_nonempty "the focus machine is in the dump" "$(printf '%s\n' "$dump" | grep -m1 '^  focus: state ')"
check_nonempty "the layout machine is in the dump" "$(printf '%s\n' "$dump" | grep -m1 '^  layout: state ')"
check_nonempty "the tag-view machine is in the dump" "$(printf '%s\n' "$dump" | grep -m1 '^  tag-view: state ')"
check_nonempty "each machine reports its entrance counts" "$(printf '%s\n' "$dump" | grep -m1 '^    entered: ')"
check_nonempty "the dump keeps the last changes of a machine" "$(printf '%s\n' "$dump" | grep -m1 -E '^      (raw|transition) ')"
check_nonempty "a change names the event that went out" "$(printf '%s\n' "$dump" | grep -m1 'event [0-9]')"

# SIGUSR1 is a question, not a request to stop: the default action for it would
# have killed the WM.
check_wm_alive

# A second request prints a second dump, so the WM is not left in a state where
# it answers once.
offset=$(wc -c <"$WM_LOG")
kill -USR1 "$WM_PID"
second=""
for _ in $(seq 1 50); do
  second=$(tail -c +$((offset + 1)) "$WM_LOG")
  case "$second" in
    *"state machines: "*) break ;;
  esac
  sleep 0.1
done
check_nonempty "a second SIGUSR1 prints a second dump" "$(printf '%s\n' "$second" | grep -m1 '^state machines: ')"

finish
