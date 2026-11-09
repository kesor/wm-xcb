#!/usr/bin/env bash
# scripts/check-doc-commands.sh exists because a document prints a command so
# the reader gets the answer without trusting the prose, and nothing ran those
# commands. docs/TESTING.md pointed `rg` at src/xcb_out.c for a macro that lives
# in src/xcbext.h: the command exits 1, prints nothing, and the document taught
# the wrong file until a reviewer read the libxcb headers by hand.
#
# This pins both directions on documents written for the occasion: a command
# that exits 0 passes and the document is named, a command that exits non-zero
# fails and the command is printed, and deleting that line from the same
# document makes it pass again. It also pins what the gate leaves alone - a
# line whose program is not on the read-only allowlist, a fence with no shell
# label, and a command naming a variable this shell does not set - because a
# gate that starts running `make format` or a session script would make every
# commit slower and less reproducible, and one that silently skips a command
# names nothing at all.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/doc-commands"
rm -rf "$DIR"
mkdir -p "$DIR"
GATE="$ROOT_DIR/scripts/check-doc-commands.sh"

# The tree as committed is the direction that has to stay green: the check runs
# on every commit, so a command in a document that answers must not fail it.
out="$DIR/tree.txt"
"$GATE" >"$out" 2>&1
status=$?
check_eq "the tree as committed passes the check" 0 "$status"
check_eq "it names docs/TESTING.md, where the rg commands are" 1 "$(grep -c 'docs/TESTING.md' "$out")"
check_eq "it says how many documents it read" 1 "$(grep -c 'document(s) read' "$out")"

# "printed nothing" needs the file to exist and not contain the pattern. The
# dev shell's libxcb tree does. A missing path makes rg print an error, and the
# assertion below would fail for the shell rather than the verdict. An empty
# file is that shape when the tree is not there.
if [ ! -f "${LIBXCB_SRC:-}/src/xcb_out.c" ]; then
  mkdir -p "$DIR/libxcb-fixture/src"
  : >"$DIR/libxcb-fixture/src/xcb_out.c"
  export LIBXCB_SRC="$DIR/libxcb-fixture"
fi

# The command the defect printed, in the shape a document writes it.
BAD="$DIR/bad.md"
cat >"$BAD" <<'EOF'
# a document whose command names the wrong file

```bash
rg -n 'REQUEST_CHECKED' "$LIBXCB_SRC/src/xcb_out.c"
```
EOF

out="$DIR/bad.txt"
"$GATE" "$BAD" >"$out" 2>&1
status=$?
check_eq "a documented command that exits non-zero fails the check" 1 "$status"
check_eq "the command is printed as the document wrote it" 1 \
  "$(grep -cF "rg -n 'REQUEST_CHECKED' \"\$LIBXCB_SRC/src/xcb_out.c\"" "$out")"
check_eq "the failure says the command printed nothing" 1 "$(grep -c 'printed nothing' "$out")"
check_eq "the failure names the document the command is in" 1 "$(grep -cF "$BAD:" "$out")"

# Deleting the line is the fix, and the same document has to pass again.
GOOD="$DIR/good.md"
grep -v 'REQUEST_CHECKED' "$BAD" >"$GOOD"
"$GATE" "$GOOD" >"$DIR/good.txt" 2>&1
status=$?
check_eq "the same document passes once the command is gone" 0 "$status"
check_eq "and it still reports the document it read" 1 \
  "$(grep -c '1 document(s) read' "$DIR/good.txt")"

# A command the allowlist does not name is not run: this gate runs on every
# commit, and `make`, `nix develop` and the session scripts are slow, mutate the
# tree, or need a display.
STAMP="$DIR/sentinel"
printf 'kept\n' >"$STAMP"
cat >"$DIR/not-run.md" <<EOF
# a document whose command is not read-only

\`\`\`bash
rm -f "$STAMP"
\`\`\`
EOF
"$GATE" "$DIR/not-run.md" >"$DIR/not-run.txt" 2>&1
status=$?
check_eq "a command outside the read-only allowlist passes the check" 0 "$status"
check_eq "and it did not run: the file it names is still there" 1 "$([ -e "$STAMP" ] && echo 1 || echo 0)"

# An unlabelled fence holds program output and C in this tree, so it is not read
# as a command. A label a document forgets is a command this gate does not see,
# which is why docs/QUALITY.md says which fences it reads.
cat >"$DIR/plain.md" <<'EOF'
# a fence with no shell label

```
rg -n 'REQUEST_CHECKED' "$LIBXCB_SRC/src/xcb_out.c"
```
EOF
"$GATE" "$DIR/plain.md" >"$DIR/plain.txt" 2>&1
status=$?
check_eq "an unlabelled fence is not run" 0 "$status"

# A command naming a variable nothing sets is reported with the command, not
# run against a path that collapsed to /src/... . WM_DOC_COMMANDS_IN_SHELL is
# what stops the gate re-entering the dev shell to look for it.
env -u LIBXCB_SRC WM_DOC_COMMANDS_IN_SHELL=1 "$GATE" "$BAD" >"$DIR/unset.txt" 2>&1
status=$?
check_eq "a command naming an unset variable does not fail the check" 0 "$status"
check_eq "it is printed as not run, with the variable named" 1 \
  "$(grep -c 'LIBXCB_SRC is not set' "$DIR/unset.txt")"
check_eq "and the command itself is printed beside it" 1 \
  "$(grep -cF "rg -n 'REQUEST_CHECKED' \"\$LIBXCB_SRC/src/xcb_out.c\"" "$DIR/unset.txt")"

# A `|` inside the pattern is the pattern, not a pipe. Skipping the line leaves
# the command unchecked, which is the failure this gate exists to catch.
cat >"$DIR/quoted-pipe.md" <<'EOF'
```bash
rg -n 'REQUEST_CHECKED|NO_SUCH_MACRO_ZZZ' "$LIBXCB_SRC/src/xcb_out.c"
```
EOF
out="$DIR/quoted-pipe.txt"
"$GATE" "$DIR/quoted-pipe.md" >"$out" 2>&1
status=$?
check_eq "a quoted alternation that matches nothing fails the check" 1 "$status"
check_eq "and the command is printed, not skipped" 1 \
  "$(grep -cF "rg -n 'REQUEST_CHECKED|NO_SUCH_MACRO_ZZZ'" "$out")"

cat >"$DIR/dq-pipe.md" <<'EOF'
```bash
rg -n "REQUEST_CHECKED|NO_SUCH_MACRO_ZZZ" "$LIBXCB_SRC/src/xcb_out.c"
```
EOF
"$GATE" "$DIR/dq-pipe.md" >"$DIR/dq-pipe.txt" 2>&1
status=$?
check_eq "a double-quoted alternation is run too" 1 "$status"

cat >"$DIR/quoted-bs.md" <<'EOF'
```bash
rg -n 'REQUEST_CHECKED\.nope' "$LIBXCB_SRC/src/xcb_out.c"
```
EOF
"$GATE" "$DIR/quoted-bs.md" >"$DIR/quoted-bs.txt" 2>&1
status=$?
check_eq "a backslash inside the pattern is not a continuation" 1 "$status"

# An unquoted pipe is still two commands. The right-hand side must not run.
PIPE_STAMP="$DIR/pipe-stamp"
cat >"$DIR/pipe.md" <<EOF
\`\`\`bash
rg -n nosuch /dev/null | touch "$PIPE_STAMP"
\`\`\`
EOF
"$GATE" "$DIR/pipe.md" >"$DIR/pipe.txt" 2>&1
status=$?
check_eq "an unquoted pipe is not run" 0 "$status"
check_eq "and its right-hand side did not run" 0 "$([ -e "$PIPE_STAMP" ] && echo 1 || echo 0)"

# A `$(...)` inside double quotes is still shell syntax. The gate must not run it.
SUB_STAMP="$DIR/sub-stamp"
cat >"$DIR/sub.md" <<EOF
\`\`\`bash
rg -n "\$(touch "$SUB_STAMP")" /dev/null
\`\`\`
EOF
"$GATE" "$DIR/sub.md" >"$DIR/sub.txt" 2>&1
status=$?
check_eq "a double-quoted command substitution is not run" 0 "$status"
check_eq "and it did not run: the stamp is absent" 0 "$([ -e "$SUB_STAMP" ] && echo 1 || echo 0)"

# The loop's stdin is the candidate list. A command that reads it consumes the
# next line, and a failing command behind it never runs.
cat >"$DIR/steal.md" <<'EOF'
```bash
grep REQUEST_CHECKED
rg -n 'REQUEST_CHECKED' "$LIBXCB_SRC/src/xcb_out.c"
```
EOF
"$GATE" "$DIR/steal.md" >"$DIR/steal.txt" 2>&1
status=$?
check_eq "a command that reads stdin does not hide the next one" 1 "$status"
check_eq "the swallowed command is still printed" 1 \
  "$(grep -cF "rg -n 'REQUEST_CHECKED' \"\$LIBXCB_SRC/src/xcb_out.c\"" "$DIR/steal.txt")"

# A `$name` inside single quotes is the pattern, not an expansion. Reporting it
# as unset skips a command the reader would run.
cat >"$DIR/sq-var.md" <<'EOF'
```bash
rg -n '$NO_SUCH_VAR_ZZZ' "$LIBXCB_SRC/src/xcb_out.c"
```
EOF
env -u NO_SUCH_VAR_ZZZ WM_DOC_COMMANDS_IN_SHELL=1 "$GATE" "$DIR/sq-var.md" >"$DIR/sq-var.txt" 2>&1
status=$?
check_eq "a single-quoted \$name is run, not skipped" 1 "$status"
check_eq "and it is not reported as a variable this shell lacks" 0 \
  "$(grep -c 'NO_SUCH_VAR_ZZZ is not set' "$DIR/sq-var.txt")"

finish
