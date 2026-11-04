# shellcheck shell=bash
#
# lib/bead-scope-report.sh - say whether a bead's work is in this worktree.
#
# Sourced by scripts/bead-scope.sh, which reads the bead and the two file lists
# from the tracker and from git, and by tests/x/test-104-bead-scope-report.sh,
# which feeds this recorded JSON and recorded file lists instead of needing a
# database or a checkout of the harness.
#
# The question is the one a worker asks and cannot answer cheaply: a bead whose
# text names a file under the harness (`../../../ai-workers-for-wm` beside this
# checkout) asks for work no commit on a `work/<bead>` branch can carry. Reading
# the harness to learn that costs a session its turns; asking this costs one
# command.
#
# The rule is evidence, not a vocabulary. A token in the bead's text is in the
# harness when it resolves, at a path boundary, to a file the harness checkout
# tracks and this repository does not. That catches `wpy/commands/publish.py`
# and a bare `publish.py`, and it does not catch `docs/README.md`, which both
# trees have. The small table in bead_scope_harness_names() covers what the file
# lists cannot: a name that belongs to the harness and has moved or been
# deleted, which is how a stale bead description misleads a worker.

# bead_scope_harness_names
#   The harness paths a bead may name, with what each one is, as
#   `<name>TAB<what it is>`. Used only when a token matches no file: a
#   description written before the harness was reorganised still names these.
bead_scope_harness_names() {
  cat <<'EOF'
worker.py	the harness entry point
worker.sh	the stub the harness left where it used to be started
wpy	the harness package
ai-workers-for-wm	the harness checkout
HANDOFF.md	the harness's rules for a session that dispatches
PROCESS.md	the harness's dispatch procedure
EOF
}

# bead_scope_text <bead-json> <comments-json>
#   Everything a bead says about itself: title, description, comments. A fix is
#   as often named in a comment as in the description, and the comment is where
#   the second occurrence of a bead's shape goes.
bead_scope_text() {
  local bead=$1 comments=$2
  jq -r '
    (if type == "array" then .[0] else . end)
    | [(.title // ""), (.description // "")]
    | join("\n")' "$bead" 2>/dev/null
  jq -r 'if type == "array" then (.[] | .text // "") else empty end' "$comments" 2>/dev/null
}

# bead_scope_tokens
#   Reads the text on stdin and prints the tokens that could name a file, sorted
#   and unique: a word with a `/` in it, or one that ends in a source extension.
#   A trailing `.` or `/` is prose, not part of a path, and a leading `./` says
#   nothing about which tree the path is in.
bead_scope_tokens() {
  awk '
    {
      count = split($0, word, /[^A-Za-z0-9_.\/-]+/)
      for (i = 1; i <= count; i++) {
        tok = word[i]
        sub(/^\.\//, "", tok)
        sub(/[.\/]+$/, "", tok)
        if (tok == "") continue
        if (tok ~ /\// || tok ~ /\.(py|sh|md|nix|toml|yml)$/) print tok
      }
    }' | sort -u
}

# bead_scope_resolve <token> <file-list>
#   The list's entries the token names at a path boundary: `publish.py` answers
#   `wpy/commands/publish.py`, and `publish.py` does not answer `mypublish.py`.
bead_scope_resolve() {
  awk -v tok="$1" '
    {
      n = length(tok)
      m = length($0)
      if ($0 == tok || (m > n && substr($0, m - n + 1) == tok && substr($0, m - n, 1) == "/")) print
    }' "$2"
}

# bead_scope_outside <bead-json> <comments-json> <repo-files> <harness-files>
#   One line per token the bead names that only the harness has, as
#   `<path>TAB<what it is>`, with the path resolved so a reader can open it.
#   Nothing when the bead's work is in this worktree.
bead_scope_outside() {
  local bead=$1 comments=$2 repo=$3 harness=$4
  local text tok base name what hit

  text=$(mktemp "${TMPDIR:-/tmp}/bead-scope-text.XXXXXX") || return 1
  bead_scope_text "$bead" "$comments" >"$text"

  {
    while IFS= read -r tok; do
      [ -n "$tok" ] || continue
      # A name both trees carry is this repository's: the question here is what
      # only the harness has.
      [ -n "$(bead_scope_resolve "$tok" "$repo")" ] && continue
      hit=$(bead_scope_resolve "$tok" "$harness" | head -1)
      if [ -n "$hit" ]; then
        printf '%s\t%s\n' "$hit" "the harness tracks it"
        continue
      fi
      base=${tok##*/}
      while IFS=$'\t' read -r name what; do
        [ -n "$name" ] || continue
        if [ "$base" = "$name" ]; then
          printf '%s\t%s\n' "$tok" "$what"
          break
        fi
      done < <(bead_scope_harness_names)
    done < <(bead_scope_tokens <"$text")
  } | sort -u

  rm -f "$text"
}

# bead_scope_verify <bead-json> <comments-json> <repo-files> <harness-files>
#   0 when the bead asks for work this worktree can carry, 3 when its work is in
#   the harness, naming what it found.
bead_scope_verify() {
  local bead=$1 comments=$2 repo=$3 harness=$4
  local id status title found why what

  IFS=$'\t' read -r id status title < <(
    jq -r '(if type == "array" then .[0] else . end)
      | [(.id // "?"), (.status // "?"), (.title // "")]
      | @tsv' "$bead" 2>/dev/null
  )

  printf 'bead-scope: %s (%s) %s\n' "$id" "$status" "$title"

  found=$(bead_scope_outside "$bead" "$comments" "$repo" "$harness")
  if [ -z "$found" ]; then
    printf '%s: nothing in its text names the harness, so its work is in this worktree.\n' "$id"
    return 0
  fi

  while IFS=$'\t' read -r what why; do
    [ -n "$what" ] || continue
    printf '  OUTSIDE  %s\n' "$what"
    printf '           %s\n' "$why"
  done <<<"$found"

  printf '%s: it names the harness, and no commit on a work/%s branch can carry a\n' "$id" "$id"
  echo "change there. Land what is in this worktree if the bead has any, record the"
  echo "rest in a bd comment, and stop: the harness is read here and not edited."
  return 3
}
