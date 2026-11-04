#!/usr/bin/env bash
# The installed dmenu writes the chosen line to stdout and exits.
#
# Every other test here runs tests/fakes/dmenu, and a stand-in answers whatever
# the test asks it: if the program it models printed something else, or stayed
# open, no assertion in this suite would notice. This case runs the installed
# dmenu on the suite's server, with a list on its stdin and a key press through
# the keyboard grab it takes, and asserts the contract the launcher depends on:
# one line on stdout, then exit.
#
# Not through the WM. A real prompt grabs the keyboard for as long as it is
# open, so a key press aimed at the WM would go to the prompt; test-92 measures
# the loop with the stand-in, which does not grab.
source "$(dirname "$0")/lib.sh"

# run.sh puts the stand-ins first on PATH, so `dmenu` resolves to the fake here.
# The rest of PATH is where the program the fake models is actually installed.
real_dmenu=""
while IFS= read -r dir; do
  [ "$dir" = "$WM_FAKE_BIN" ] && continue
  if [ -x "$dir/dmenu" ]; then
    real_dmenu="$dir/dmenu"
    break
  fi
done < <(printf '%s\n' "$PATH" | tr ':' '\n')

check_nonempty "the installed dmenu is on PATH behind the stand-in" "$real_dmenu"
if [ -z "$real_dmenu" ]; then
  finish
  exit 1
fi

work="$TMPDIR_SEAM/dmenu-selection-$$-$RANDOM"
mkdir -p "$work"
out="$work/stdout"
err="$work/stderr"

printf 'alpha\nbeta\ngamma\n' | "$real_dmenu" -p selection-case >"$out" 2>"$err" &
dmenu_pid=$!

prompt=""
for _ in $(seq 1 250); do
  prompt=$(xdotool search --class '^dmenu$' 2>/dev/null | head -1)
  [ -n "$prompt" ] && break
  sleep 0.02
done
if [ -z "$prompt" ]; then
  kill "$dmenu_pid" 2>/dev/null
  fail "the installed dmenu drew no prompt within 5 s, so this case cannot check what it writes"
  finish
  exit 1
fi
pass "the installed dmenu drew a prompt"

# The window is mapped before the keyboard grab is in place, so a keystroke sent
# here races the grab; the prompt is still open when a key is lost, and the wait
# below fails rather than the assertion passing on the wrong text.
xdotool type --delay 10 alpha
xdotool key Return

exited=0
for _ in $(seq 1 150); do
  if ! kill -0 "$dmenu_pid" 2>/dev/null; then
    exited=1
    break
  fi
  sleep 0.02
done
if [ "$exited" -eq 0 ]; then
  kill "$dmenu_pid" 2>/dev/null
  fail "the installed dmenu stayed open after the selection: the launcher would wait for a pipe that never ends"
  finish
  exit 1
fi

wait "$dmenu_pid"
rc=$?
check_eq "the installed dmenu exits 0 after a selection" "0" "$rc"
check_eq "the installed dmenu writes the chosen line to stdout" "alpha" "$(cat "$out")"
check_eq "the installed dmenu writes nothing to stderr" "" "$(cat "$err")"

rm -rf "$work"
finish
