#!/usr/bin/env bash
# scripts/check-bead-create-recipe.sh guards the `bd create` a worker copies: a
# recipe with no --acceptance files a bead that scripts/check-bead-acceptance.sh,
# the gate the recipe itself should have satisfied, refuses at the next commit.
# A skill file is written by `bd setup` from bd's own template, so the flag is
# not something the tree can fix once.
#
# The shapes pinned here are the ones a reader of a document has to tell apart:
#
#   - a recipe in a code block, with and without the flag;
#   - a command that runs past its line, by a trailing backslash and by a
#     double-quoted argument left open, where judging one line fails a recipe
#     that carries the flag further down;
#   - a sentence about the command, which is not a recipe;
#   - a recipe written inside a sentence, in backticks, which is;
#   - the tree as committed, which every worker copies from.
source "$(dirname "$0")/lib.sh"

DIR="$TMPDIR_SEAM/bead-create-recipe"
rm -rf "$DIR"
mkdir -p "$DIR"

GATE="$ROOT_DIR/scripts/check-bead-create-recipe.sh"

# doc <name> - writes a document whose third line starts the shape under test,
# so the file passes the rest of the fixture on stdin, and prints its path.
doc() {
  local path="$DIR/$1"
  {
    printf '# fixture\n\n'
    cat
  } >"$path"
  printf '%s' "$path"
}

gate() { "$GATE" "$1" 2>&1; }

MISSING=$(doc missing.md <<'EOF'
bd create "Short title" --type=task --priority=2
EOF
)
out=$(gate "$MISSING")
check_eq "a code-block recipe with no --acceptance fails" 1 "$?"
check_eq "and the file and line are named" 1 "$(printf '%s' "$out" | grep -c "NO ACCEPTANCE .*$MISSING:3")"
check_eq "and the fix says what the flag carries" 1 \
  "$(printf '%s' "$out" | grep -c 'Write --acceptance with what a command will show')"

PASSING=$(doc passing.md <<'EOF'
bd create "Short title" --type=task --acceptance "the suite reports zero failures"
EOF
)
out=$(gate "$PASSING")
check_eq "the same recipe with the flag passes" 0 "$?"
check_eq "and prints nothing" 0 "$(printf '%s' "$out" | grep -c 'NO ACCEPTANCE')"

# The wm-5s shape: the flag is further down a command continued with backslashes.
WRAPPED=$(doc wrapped.md <<'EOF'
bd create --type=chore --priority=1 \
  --title "friction(seiton): lefthook missing from the worker shell" \
  --acceptance "lefthook run pre-commit passes in the worker shell" \
  --description "What I tried: lefthook run pre-commit"
EOF
)
out=$(gate "$WRAPPED")
check_eq "a recipe continued with a backslash is read whole" 0 "$?"
check_eq "and its flag anywhere in the command counts" 0 "$(printf '%s' "$out" | grep -c 'NO ACCEPTANCE')"

# A --description that spans lines keeps the shell string open, so the command
# continues without a backslash on the quoting line.
DESCRIBED=$(doc described.md <<'EOF'
bd create --title "friction(seiton): something" --description "What I tried: one command
What happened: nothing" \
  --acceptance "the gate names the line"
EOF
)
out=$(gate "$DESCRIBED")
check_eq "an open quote carries the command onto the next line" 0 "$?"
check_eq "and the flag after the description counts" 0 "$(printf '%s' "$out" | grep -c 'NO ACCEPTANCE')"

# The mention in AGENTS.md, which ends at the command name.
MENTION=$(doc mention.md <<'EOF'
A bead is created with its acceptance criteria. `bd create
--acceptance` is the only write a worker may make to that field.
EOF
)
out=$(gate "$MENTION")
check_eq "a sentence that ends at the command name is not a recipe" 0 "$?"
check_eq "and is not reported" 0 "$(printf '%s' "$out" | grep -c 'NO ACCEPTANCE')"

# A sentence about the command, which has no command after the name either.
ABOUT=$(doc about.md <<'EOF'
`bd create` is not in this tree and cannot be made to refuse the field.
EOF
)
out=$(gate "$ABOUT")
check_eq "a sentence about the command is not a recipe" 0 "$?"

BARE=$(doc bare.md <<'EOF'
bd create
EOF
)
out=$(gate "$BARE")
check_eq "a bare command name with nothing after it is not a recipe" 0 "$?"

INLINE=$(doc inline.md <<'EOF'
File the follow-up with `bd create --title "..."` and commit it.
EOF
)
out=$(gate "$INLINE")
check_eq "a recipe written inside a sentence fails too" 1 "$?"
check_eq "and is reported on its line" 1 "$(printf '%s' "$out" | grep -c "NO ACCEPTANCE .*$INLINE:3")"

PROMPT=$(doc prompt.md <<'EOF'
$ bd create "Try out Beads"
EOF
)
out=$(gate "$PROMPT")
check_eq "a recipe after a shell prompt is a recipe" 1 "$?"

# Headings and fenced markers are not commands, and neither is a line that only
# points at the command's help.
HELP=$(doc help.md <<'EOF'
Run `bd create --help` to see the flags.
EOF
)
out=$(gate "$HELP")
check_eq "a document that points at the help text is not a recipe" 0 "$?"

# The tree as committed: every document a worker can copy from.
out=$(cd "$ROOT_DIR" && "$GATE" 2>&1)
check_eq "the tree as committed carries the flag everywhere" 0 "$?"
check_eq "and nothing is reported" 0 "$(printf '%s' "$out" | grep -c 'NO ACCEPTANCE')"

finish
