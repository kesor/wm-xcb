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
# text names a file under the harness (the checkout beside the main one) asks
# for work no commit on a `work/<bead>` branch can carry. Reading the harness to
# learn that costs a session its turns; asking this costs one command.
#
# The rule is evidence, not a vocabulary. A token in the bead's text is in the
# harness when it resolves, at a path boundary, to a file the harness checkout
# tracks and this repository does not. That catches `wpy/commands/publish.py`
# and a bare `publish.py`, and it does not catch `docs/README.md`, which both
# trees have. The small table in bead_scope_harness_names() covers what the file
# lists cannot: a name that belongs to the harness and has moved or been
# deleted, which is how a stale bead description misleads a worker.

# bead_scope_harness_dir <repo-root>
#   The harness checkout beside the main checkout. Three levels up from a
#   worktree under .worktrees/<bead> happens to land there; the same three
#   levels up from the main checkout does not. Dispatch runs this from the main
#   checkout, before a worktree exists, so the path is computed from the common
#   git dir rather than from where the script file sits. WORKER_HARNESS
#   overrides it. The three-up path is the fallback when git cannot name the
#   common dir, and it is wrong from the main checkout.
bead_scope_harness_dir() {
  local root=$1 common main parent
  if [ -n "${WORKER_HARNESS:-}" ]; then
    printf '%s\n' "$WORKER_HARNESS"
    return 0
  fi
  common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || common=""
  if [ -z "$common" ]; then
    printf '%s\n' "$root/../../../ai-workers-for-wm"
    return 0
  fi
  main=$(dirname "$common")
  parent=$(cd "$main/.." && pwd) || {
    printf '%s\n' "$root/../../../ai-workers-for-wm"
    return 0
  }
  printf '%s\n' "$parent/ai-workers-for-wm"
}

# bead_scope_harness_names
#   The harness paths a bead may name, with what each one is, as
#   `<name>TAB<what it is>`. Used only when a token matches no file: a
#   description written before the harness was reorganised still names these,
#   and a path whose component is one of them (`wpy/commands/publish.py`, or
#   `wpy/commands` left after a brace list is split) still names the harness
#   when the file list cannot resolve it.
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
#   One line per harness path the bead names, as `<path>TAB<what it is>`.
#   A token that resolves against the file list is printed as that file. A token
#   that only matches a known harness name is printed as written. Two tokens that
#   name the same file are one line. Nothing when no token names the harness.
bead_scope_outside() {
  local bead=$1 comments=$2 repo=$3 harness=$4
  local text tok name what hit rest part matched path

  text=$(mktemp "${TMPDIR:-/tmp}/bead-scope-text.XXXXXX") || return 1
  bead_scope_text "$bead" "$comments" >"$text"

  {
    while IFS= read -r tok; do
      [ -n "$tok" ] || continue
      # A name both trees carry is this repository's: the question here is what
      # only the harness has.
      [ -n "$(bead_scope_resolve "$tok" "$repo")" ] && continue
      hit=$(bead_scope_resolve "$tok" "$harness")
      if [ -n "$hit" ]; then
        while IFS= read -r path; do
          [ -n "$path" ] || continue
          printf '%s\t%s\n' "$path" "the harness tracks it"
        done <<<"$hit"
        continue
      fi
      rest=$tok
      while [ -n "$rest" ]; do
        part=${rest%%/*}
        matched=0
        while IFS=$'\t' read -r name what; do
          [ -n "$name" ] || continue
          if [ "$part" = "$name" ]; then
            printf '%s\t%s\n' "$tok" "$what"
            matched=1
            break
          fi
        done < <(bead_scope_harness_names)
        if [ "$matched" -eq 1 ]; then
          break
        fi
        case "$rest" in
          */*) rest=${rest#*/} ;;
          *) break ;;
        esac
      done
    done < <(bead_scope_tokens <"$text")
  } | sort -u

  rm -f "$text"
}

# bead_scope_verify <bead-json> <comments-json> <repo-files> <harness-files>
#   0 when no token names the harness, 3 when one does, naming what it found.
#   A bead can name a harness file and still ask for a change in this tree; the
#   printed verdict says to land that part.
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
