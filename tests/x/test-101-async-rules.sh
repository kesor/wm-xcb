#!/usr/bin/env bash
# The rule list in scripts/check-async.sh is a set of regexes with an exception
# list next to it, and nothing checked what either one does. A call libxcb
# documents as not waiting was reported as a violation, so the author's choices
# were a waiver whose text would be false or an edit to the gate. The other half
# is where a match is read from: a sentence inside a /* ... */ block that began a
# continuation line with waitpid( was reported as a blocking wait, and a call
# named in a // comment or a string was read as code.
#
# This runs the gate over probe files, so the rule list, the exceptions and the
# reading of prose are judged by the suite that already covers the other gates.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/async-scan.sh
. "$ROOT_DIR/scripts/lib/async-scan.sh"

DIR="$TMPDIR_SEAM/async-rules"
rm -rf "$DIR"
mkdir -p "$DIR/bin"

BLOCKING="$DIR/fixture-blocking.c"
cat >"$BLOCKING" <<'EOF'
static void
probe(void)
{
  xcb_wait_for_reply(dpy, cookie, NULL);
  xcb_get_property_replies(dpy, cookie, 0, atoms);
  xcb_request_check(dpy, cookie);
  xcb_aux_sync(dpy);
  xcb_key_symbols_get_keycode(syms, XK_a);
  xcb_key_symbols_get_keysym(syms, keycode, 0);
  read(fd, buf, 1);
  waitpid(pid, &st, 0);
  usleep(10);
  sleep(1);
  nanosleep(&ts, NULL);
  /* a block comment that closes here */ waitpid(-1, &other, 0);
  waitpid(-1, &hidden, 0); /* WNOHANG */
}
EOF

ALLOWED="$DIR/fixture-allowed.c"
cat >"$ALLOWED" <<'EOF'
static const char *prose =
    "xcb_wait_for_reply(dpy, cookie) inside a string is not a call";
/* Every child the WM starts is reaped here. The handler runs on SIGCHLD, and
   waitpid(). The launcher learns what dmenu chose from its pipe, and it is
   read(). The descriptor is one of the loop's event sources. */
static void
probe(void)
{
  int x = 1; // waitpid(pid, &st, 0);
  // read(fd, buf, 1);
  xcb_poll_for_reply(dpy, seq, &reply, &err);
  xcb_poll_for_reply64(dpy, seq, &reply, &err);
  xcb_discard_reply(dpy, seq);
  waitpid(pid, &st, WNOHANG);
  /* allow-blocking: the read end is O_NONBLOCK, so it returns EAGAIN */
  read(fd, buf, 1);
}
EOF

# The gate asks the build for its source list. A make that answers with these
# fixtures runs the gate over them instead of the tree, which is how the run the
# hook makes is the one judged here.
cat >"$DIR/bin/make" <<EOF
#!/usr/bin/env bash
if [ "\$*" = "-s print-src" ]; then
  cat "$DIR/sources.txt"
  exit 0
fi
exit 1
EOF
chmod +x "$DIR/bin/make"

# run_gate <source file> - prints the gate's output and sets GATE_STATUS.
run_gate() {
  printf '%s\n' "$1" >"$DIR/sources.txt"
  PATH="$DIR/bin:$PATH" "$ROOT_DIR/scripts/check-async.sh" >"$DIR/gate.log" 2>&1
  GATE_STATUS=$?
  cat "$DIR/gate.log"
}

line_of() { grep -nF "$1" "$2" | head -1 | cut -d: -f1; }

# The blocking fixture has one real call per line, so the set of reported line
# numbers is the answer to "is each rule still doing its job".
EXPECTED=()
for call in \
  'xcb_wait_for_reply(dpy, cookie, NULL);' \
  'xcb_get_property_replies(dpy, cookie, 0, atoms);' \
  'xcb_request_check(dpy, cookie);' \
  'xcb_aux_sync(dpy);' \
  'xcb_key_symbols_get_keycode(syms, XK_a);' \
  'xcb_key_symbols_get_keysym(syms, keycode, 0);' \
  'read(fd, buf, 1);' \
  'waitpid(pid, &st, 0);' \
  'usleep(10);' \
  'sleep(1);' \
  'nanosleep(&ts, NULL);' \
  'waitpid(-1, &other, 0);' \
  'waitpid(-1, &hidden, 0); /* WNOHANG */'; do
  EXPECTED+=("$(line_of "$call" "$BLOCKING")")
done

run_gate "$BLOCKING" >/dev/null
check_eq "a tree with blocking calls fails the gate" 1 "$GATE_STATUS"

awk -v f="$BLOCKING" '
  $1 == "NEW" && $2 == "VIOLATION" && index($3, f ":") == 1 {
    line = $3
    sub(/^.*:/, "", line)
    print line
  }
' "$DIR/gate.log" >"$DIR/reported-lines.txt"
mapfile -t REPORTED < <(sort -n -u "$DIR/reported-lines.txt")
check_eq "every call the rule list names is reported" \
  "$(printf '%s' "${EXPECTED[*]}")" "$(printf '%s' "${REPORTED[*]}")"
# sort -u would hide a line two rules both reported. The count is the raw hits.
check_eq "and no line is reported twice by two rules" \
  "${#EXPECTED[@]}" "$(grep -c . "$DIR/reported-lines.txt" || true)"

check_eq "a call after a comment that closes on the same line is reported" 1 \
  "$(grep -cE "NEW VIOLATION  .*:$(line_of 'waitpid(-1, &other, 0);' "$BLOCKING") " "$DIR/gate.log")"

run_gate "$ALLOWED" >/dev/null
check_eq "a file with only prose and non-blocking calls passes the gate" 0 "$GATE_STATUS"
check_eq "the waived call is counted, not judged as a violation" 1 \
  "$(grep -cE '^check-async: no new violations \(1 files, 0 known, 1 waived, 0 refused\)$' "$DIR/gate.log")"

for prose in 'waitpid(). The launcher' 'read(). The descriptor' 'inside a string is not a call' '// waitpid(pid' '// read(fd'; do
  check_eq "no violation for: $prose" 0 \
    "$(grep -c "NEW VIOLATION.*$prose" "$DIR/gate.log")"
done

# The gate reports a line number from the copy it reads, so the copy has to keep
# the file's line structure exactly.
check_eq "the copy keeps the file's line count" \
  "$(wc -l <"$ALLOWED")" "$(async_strip_comments "$ALLOWED" | wc -l)"

STRING_LINE=$(line_of 'inside a string is not a call' "$ALLOWED")
STRING_COPY=$(async_strip_comments "$ALLOWED" | sed -n "${STRING_LINE}p")
check_eq "a string keeps its quotes so the line still lines up" 2 \
  "$(printf '%s' "$STRING_COPY" | tr -cd '"' | wc -c)"
check_eq "and its contents are blanked" 0 \
  "$(printf '%s' "$STRING_COPY" | grep -c 'wait_for_reply')"

finish
