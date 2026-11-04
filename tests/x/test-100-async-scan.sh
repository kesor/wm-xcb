#!/usr/bin/env bash
# An allow-blocking comment waived a reply in a helper the MappingNotify
# handler reached through keybinding_grab_keys, on the strength of a reason
# that was true only for the startup caller. check-async now refuses a waiver
# inside a function the event loop can reach, and this pins that decision
# against small sources, so it does not need the tree to contain the defect.
source "$(dirname "$0")/lib.sh"
# shellcheck source=../../scripts/lib/async-scan.sh
. "$ROOT_DIR/scripts/lib/async-scan.sh"

DIR="$TMPDIR_SEAM/async-scan"
rm -rf "$DIR"
mkdir -p "$DIR/bin"

# The shape below is the one clang-format writes: a name at column 0, the body
# opening on the next line, and a registration wrapped over three lines.
LOOP="$DIR/fixture-loop.c"
cat >"$LOOP" <<'EOF'
static void
reply_helper(void)
{
  /* allow-blocking: startup only, the event loop has not started yet */
  xcb_get_modifier_mapping_reply(dpy, cookie, NULL);
}

static void
on_map_request(void* event)
{
  reply_helper();
}

void
register_handlers(void)
{
  xcb_handler_register(
      XCB_MAP_REQUEST,
      &component,
      on_map_request);
}

static void
on_expose(void* event)
{
  reply_helper();
}

static void
on_no_exposure(void* event)
{
  reply_helper();
}

void
register_expose(void)
{
  /* clang-format keeps a trailing comment on the closing line, spaces and all,
     and the comment may itself contain parentheses. */
  xcb_handler_register(
      XCB_EXPOSE,
      &component,
      on_expose); /* expose (the event) */
  xcb_handler_register(XCB_NO_EXPOSURE, &component, on_no_exposure); // no exposure (copy)
}

static void
startup_only(void)
{
  /* allow-blocking: startup, before the loop runs */
  xcb_query_tree_reply(dpy, cookie, NULL);
}

static void
unwaived(void)
{
  xcb_get_property_reply(dpy, cookie, NULL);
}

static void
bare_marker(void)
{
  /* allow-blocking: */
  xcb_get_screen_resources_reply(dpy, cookie, NULL);
}
EOF

# A second file: a handler registered on one line, calling a helper in the
# first file, so reachability is followed across files.
OTHER="$DIR/fixture-other.c"
cat >"$OTHER" <<'EOF'
static void
on_key_press(void* event)
{
  reply_helper();
}

void
register_more(void)
{
  xcb_handler_register(XCB_KEY_PRESS, &component, on_key_press);
}
EOF

async_scan_init "$LOOP" "$OTHER"

line_of() { grep -n "$1" "$2" | head -1 | cut -d: -f1; }
HELPER_CALL=$(line_of 'xcb_get_modifier_mapping_reply' "$LOOP")
STARTUP_CALL=$(line_of 'xcb_query_tree_reply' "$LOOP")
UNWAIVED_CALL=$(line_of 'xcb_get_property_reply' "$LOOP")
BARE_CALL=$(line_of 'xcb_get_screen_resources_reply' "$LOOP")

async_loop_callable on_map_request
check_eq "a handler named in a wrapped registration is callable" 0 "$?"

async_loop_callable on_expose
check_eq "a wrapped registration with a trailing block comment names the handler" 0 "$?"

async_loop_callable on_no_exposure
check_eq "a one-line registration with a trailing line comment names the handler" 0 "$?"

async_loop_callable on_key_press
check_eq "a handler named in a one-line registration is callable" 0 "$?"

async_loop_callable register_handlers
check_eq "the function that registers a handler is not itself a handler" 1 "$?"

async_loop_callable reply_helper
check_eq "a helper a handler calls is callable" 0 "$?"

async_loop_callable startup_only
check_eq "a helper no handler reaches is not callable" 1 "$?"

async_loop_callable register_more
check_eq "registering from a second file does not make it callable" 1 "$?"

check_eq "the wrapper names the function the call sits in" reply_helper \
  "$(async_enclosing_function "$LOOP" "$HELPER_CALL")"
check_eq "a startup helper is named too" startup_only \
  "$(async_enclosing_function "$LOOP" "$STARTUP_CALL")"

check_eq "a reason in a callable function is refused" refused \
  "$(async_waiver_verdict "$LOOP" "$HELPER_CALL")"
check_eq "the same reason outside the loop is waived" waived \
  "$(async_waiver_verdict "$LOOP" "$STARTUP_CALL")"
check_eq "a call with no reason above it is not waived" none \
  "$(async_waiver_verdict "$LOOP" "$UNWAIVED_CALL")"
check_eq "a marker with nothing after it is not a waiver" unreasoned \
  "$(async_waiver_verdict "$LOOP" "$BARE_CALL")"

# The gate reads its source list from the build. A make that prints this
# fixture's files answers it without the tree's own sources, so the run the
# hook makes is the one judged here.
cat >"$DIR/bin/make" <<EOF
#!/usr/bin/env bash
if [ "\$*" = "-s print-src" ]; then
  echo "$LOOP $OTHER"
  exit 0
fi
exit 1
EOF
chmod +x "$DIR/bin/make"

OUT="$DIR/gate.log"
PATH="$DIR/bin:$PATH" "$ROOT_DIR/scripts/check-async.sh" >"$OUT" 2>&1
STATUS=$?

check_eq "the gate fails a refused waiver" 1 "$STATUS"
check_eq "it names the line and the function to the reader" 1 \
  "$(grep -c "WAIVER REFUSED  $LOOP:$HELPER_CALL  reply_helper is reachable from an XCB handler" "$OUT")"
check_eq "the refused call is judged as a violation" 1 \
  "$(grep -c "  NEW VIOLATION  $LOOP:$HELPER_CALL  " "$OUT")"
check_eq "the count separates the three outcomes" 1 \
  "$(grep -cE '^check-async: 3 new violation\(s\), [0-9]+ known, 1 waived, 1 refused$' "$OUT")"
check_eq "the startup-only reason is not counted as a violation" 0 \
  "$(grep -c "  NEW VIOLATION  $LOOP:$STARTUP_CALL  " "$OUT")"
check_eq "a marker with nothing after it is reported" 1 \
  "$(grep -cE "WAIVER WITHOUT A REASON  $LOOP:$BARE_CALL([^0-9]|$)" "$OUT")"

finish
