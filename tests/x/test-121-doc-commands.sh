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
# document makes it pass again.
#
# It also pins the cap, which is the part that took two review rounds. The gate
# never hands a document's line to a shell; it splits the line itself, so the
# words it checks are the words the program receives. What it prints as not run
# is therefore what it does not execute: a line whose words a shell would build
# from a brace, a glob, an ANSI-C quote or a parameter of the gate's own
# command line, and a flag that is not on its list - whatever the flag is
# spelled like, how it is abbreviated, or which short-option cluster it hides
# in. A wrapper that touches a stamp when it runs is the witness: the stamp is
# there when the same wrapper is run by hand, and absent after every case here.
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

# A command the allowlist does not name is not run and not reported: this gate
# runs on every commit, and `make`, `nix develop` and the session scripts are
# slow, mutate the tree, or need a display.
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
check_eq "and it is not printed as a line the gate left alone" 0 \
  "$(grep -c 'NOT RUN' "$DIR/not-run.txt")"

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

# A `|` inside the pattern is the pattern, not a pipe. The gate splits the line
# itself, so the alternation reaches rg as one word and an empty answer fails.
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
check_eq "a backslash inside the pattern is not an escape the gate eats" 1 "$status"

# An unquoted pipe is two commands, and the gate runs neither.
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

# A `$(...)` is a command substitution wherever it is written. The gate builds
# the argument list itself, so there is nothing to substitute.
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

# A command that reads standard input must not eat the next candidate: the
# gate's own input is the list of lines still to check.
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

# An allowlisted program reads what it is pointed at and exits, unless an
# argument hands it another program to run: rg --pre, git grep
# --open-files-in-pager, git --ext-diff. The flagship of the cap is that a
# spelling the gate has never seen is not run either, so the program cannot be
# reached by inventing one. The wrapper leaves a stamp when it runs at all.
PROG_STAMP="$DIR/prog-stamp"
cat >"$DIR/wrapper" <<EOF
#!/bin/sh
touch "$PROG_STAMP"
cat "\$1" 2>/dev/null
exit 0
EOF
chmod +x "$DIR/wrapper"

"$DIR/wrapper" /dev/null
check_eq "the wrapper leaves its stamp when something runs it" 1 \
  "$([ -e "$PROG_STAMP" ] && echo 1 || echo 0)"
rm -f "$PROG_STAMP"

# unrun_case <name> - the document on stdin passes the check, its line is
# printed as not run, and the wrapper's stamp is absent afterwards.
unrun_case() {
  local name=$1 doc="$DIR/$1.md" status
  cat >"$doc"
  "$GATE" "$doc" >"$DIR/$1.txt" 2>&1
  status=$?
  check_eq "$name: the check passes" 0 "$status"
  check_eq "$name: the line is printed as not run" 1 \
    "$(grep -c 'NOT RUN' "$DIR/$1.txt")"
  check_eq "$name: the program it named did not run" 0 \
    "$([ -e "$PROG_STAMP" ] && echo 1 || echo 0)"
}

# The spellings a reviewer writes when told the gate skips a flag: the literal
# one, the one in an ANSI-C quote, the one hidden in a short-option cluster, the
# one git accepts as an abbreviation, and the one a brace would build.
unrun_case rg-pre <<EOF
\`\`\`bash
rg --pre "$DIR/wrapper" pattern .
\`\`\`
EOF

unrun_case rg-pre-equals <<EOF
\`\`\`bash
rg --pre="$DIR/wrapper" pattern .
\`\`\`
EOF

unrun_case rg-pre-quoted <<EOF
\`\`\`bash
rg "--pre=$DIR/wrapper" pattern .
\`\`\`
EOF

unrun_case rg-pre-ansi <<EOF
\`\`\`bash
rg \$'--pre=$DIR/wrapper' pattern .
\`\`\`
EOF

unrun_case pager-long <<EOF
\`\`\`bash
git grep --open-files-in-pager="$DIR/wrapper" pattern
\`\`\`
EOF

unrun_case pager-short <<EOF
\`\`\`bash
git grep -O"$DIR/wrapper" pattern
\`\`\`
EOF

unrun_case pager-cluster <<EOF
\`\`\`bash
git grep -nO$DIR/wrapper pattern
\`\`\`
EOF

unrun_case pager-abbrev <<EOF
\`\`\`bash
git grep --open-files-in-p=$DIR/wrapper pattern
\`\`\`
EOF

unrun_case pager-ansi <<EOF
\`\`\`bash
git grep \$'--open-files-in-pager=$DIR/wrapper' pattern
\`\`\`
EOF

unrun_case pager-brace <<EOF
\`\`\`bash
git grep --open-files-in-page{r,}=$DIR/wrapper pattern
\`\`\`
EOF

# git --ext-diff runs GIT_EXTERNAL_DIFF, so this case sets it: with the gate
# skipping the flag, the stamp stays absent because git never ran the wrapper.
unrun_case ext-diff <<'EOF'
```bash
git log -p -1 --ext-diff
```
EOF

unrun_case ext-diff-ansi <<'EOF'
```bash
git log -p -1 $'--ext-diff'
```
EOF

# A flag nobody has looked at is not run either. This is the cap: the list is
# the whole of what the gate hands a program, so there is no spelling to find.
unrun_case unknown-flag <<'EOF'
```bash
rg -q pattern docs/README.md
```
EOF

# $1 is the gate's own first argument. The hook passes none; a by-hand run
# passes a document. Neither is the document's reader.
param="\$1"
cat >"$DIR/positional.md" <<'EOF'
```bash
rg -n REQUEST_CHECKED "$1"
```
EOF
"$GATE" "$DIR/positional.md" >"$DIR/positional.txt" 2>&1
status=$?
check_eq "a command naming the gate's own \$1 does not fail the check" 0 "$status"
check_eq "it is printed as not run, with the parameter named" 1 \
  "$(grep -cF "$param is a parameter of this gate" "$DIR/positional.txt")"

cat >"$DIR/parameter-default.md" <<'EOF'
```bash
rg -n REQUEST_CHECKED ${1:-}
```
EOF
"$GATE" "$DIR/parameter-default.md" >"$DIR/parameter-default.txt" 2>&1
status=$?
check_eq "a \${1:-} expansion does not fail the check" 0 "$status"
check_eq "and it is printed as not run too" 1 \
  "$(grep -c 'NOT RUN' "$DIR/parameter-default.txt")"

cat >"$DIR/parameter-plus.md" <<EOF
\`\`\`bash
rg -n REQUEST_CHECKED \${1:+--pre=$DIR/wrapper}
\`\`\`
EOF
"$GATE" "$DIR/parameter-plus.md" >"$DIR/parameter-plus.txt" 2>&1
status=$?
check_eq "a \${1:+...} expansion does not fail the check" 0 "$status"
check_eq "and the wrapper it would build did not run" 0 \
  "$([ -e "$PROG_STAMP" ] && echo 1 || echo 0)"

# A variable that is set can carry a flag, and the shell is where a document
# would put it. The gate expands the name itself, so the flag reaches its own
# check before it reaches the program.
cat >"$DIR/env-flag.md" <<'EOF'
```bash
rg $PRE pattern docs/README.md
```
EOF
env WM_DOC_COMMANDS_IN_SHELL=1 PRE="--pre=$DIR/wrapper" "$GATE" "$DIR/env-flag.md" \
  >"$DIR/env-flag.txt" 2>&1
status=$?
check_eq "a variable holding a flag does not fail the check" 0 "$status"
check_eq "it is printed as not run, with the flag named" 1 \
  "$(grep -c -- '--pre=' "$DIR/env-flag.txt")"
check_eq "and the wrapper the variable held did not run" 0 \
  "$([ -e "$PROG_STAMP" ] && echo 1 || echo 0)"

# A scanner that fails leaves the document unchecked. Reporting that as a pass
# is the defect this gate exists to catch, one level down.
mkdir -p "$DIR/bin"
cat >"$DIR/bin/awk" <<'EOF'
#!/bin/sh
exit 2
EOF
chmod +x "$DIR/bin/awk"
env WM_DOC_COMMANDS_IN_SHELL=1 PATH="$DIR/bin:$PATH" "$GATE" "$GOOD" \
  >"$DIR/scanner.txt" 2>&1
status=$?
check_eq "a scanner that fails fails the check" 1 "$status"
check_eq "and it says the file was not checked" 1 \
  "$(grep -c 'is not a pass' "$DIR/scanner.txt")"

finish
