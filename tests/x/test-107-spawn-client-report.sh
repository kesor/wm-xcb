#!/usr/bin/env bash
# A spawn_client that gets no window id reports a harness failure, not a WM
# defect. xclient writes its ids to a file the parent creates before the fork,
# so a shortfall means the client never ran; every caller's own guard turns the
# empty list into "focus is not on either managed client", which reads as a WM
# regression and is what one lost read cost a whole cycle on 2026-11-04.
#
# The second half pins the other direction: five spawns back to back each return
# the id xclient wrote. A read that beats the client, or one that errors with
# "[: : integer expected" and falls out of the wait loop, shows up here as zero.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/spawn-failure"
rm -rf "$DIR"
mkdir -p "$DIR/bin"

# The shape of a client that died before it created a window.
printf '%s\n' '#!/usr/bin/env bash' 'exit 1' >"$DIR/bin/xclient"
chmod +x "$DIR/bin/xclient"

# A file shaped like a seam test: spawn, then make the assertion every caller
# makes when the id list came back empty. It is run as its own process because
# the failure under test stops the file that hit it.
cat >"$DIR/spawns-nothing.sh" <<'EOF'
#!/usr/bin/env bash
source "$LIB_SH"
XCLIENT="$FAKE_XCLIENT"
mapfile -t ids < <(spawn_client 1 400 300 blameless)
fail "focus is not on either managed client ('${ids[0]:-}'), so there is nothing to follow"
finish
EOF

OUT="$DIR/out"
LIB_SH="$ROOT_DIR/tests/x/lib.sh" FAKE_XCLIENT="$DIR/bin/xclient" \
  bash "$DIR/spawns-nothing.sh" >"$OUT" 2>&1
rc=$?
printf '  the empty spawn ran as %s in %s\n' "$rc" "$OUT"

check_eq "a spawn with no window id fails the file" 1 "$rc"
check_eq "the failure names the harness" 1 "$(grep -c '^  FAIL  spawn_client: ' "$OUT")"
check_eq "the file's WM-shaped assertion never runs" 0 \
  "$(grep -c 'focus is not on either managed client' "$OUT")"
check_eq "the file still prints its own count" 1 "$(grep -c '^  0 passed, 1 failed$' "$OUT")"
check_eq "a count cannot print an empty string into a test" 0 \
  "$(grep -c 'integer expected' "$OUT")"
check_eq "an output file that cannot be read counts as zero lines" 0 \
  "$(output_lines "$DIR/missing")"

for i in 1 2 3 4 5; do
  mapfile -t ids < <(spawn_client 1 200 150 tightloop)
  check_eq "spawn $i of a back-to-back run returned one id" 1 "${#ids[@]}"
done

finish
