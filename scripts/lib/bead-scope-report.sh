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
#
# A path is not the only way a bead names the harness. A fix there is often one
# of its commands - `cmd_start`, `cmd_drop` - and the bead then says so in those
# words and names no file, so the token scan finds nothing and the verdict reads
# "its work is in this worktree": the answer that costs the session this command
# exists to save. bead_scope_harness_words() reads the text for `harness` and
# for a `cmd_` command, and the verdict then says it cannot tell, which is 2.
# `worktree` is not one of the words: every bead here says "in this worktree",
# and a rule that fired on it would answer "cannot tell" for most of the queue,
# which says nothing at all. The word `harness` does fire on the few beads about
# this tree's own seam harness, and that answer costs a look at the bead's text;
# the 0 it replaces costs a read of the harness.
#
# The harness is only half of the question a worker asks. The other half is
# whether the paths the bead names still exist here: a bead is written months
# before it is picked up, and one that asks for changes to a file this tree
# deleted reads exactly like one that asks for changes to a file it has. The
# file list alone cannot tell those apart, and a tool that answers "workable
# here" for both sends the worker to resurrect a file its own history removed.
# So a token this tree does not carry is a finding too: the path, the deletion
# commit when git records one, and whether the directory that held it is still
# here. That answer is "cannot tell", not "here" and not "not here": a new file
# looks the same from the text as one another bead delivered under another name,
# so the verdict says to grep for the behaviour first, and the caller looks.

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

# bead_scope_harness_words <bead-json> <comments-json>
#   The words in the text that name the harness rather than a file in it, sorted
#   and unique: `harness`, and a command of the harness checkout, which it names
#   `cmd_<verb>` (`cmd_start`, `cmd_drop`). No file in that checkout is a
#   `cmd_` name and nothing in this repository is either, so the word is the
#   command and not a guess.
#
#   This reads the text again instead of reusing bead_scope_tokens(): that
#   tokeniser splits on everything but a path's characters, so `cmd_start`
#   reaches it as `cmd` and `start`, and it keeps only a word with a `/` or a
#   source extension.
bead_scope_harness_words() {
  bead_scope_text "$1" "$2" |
    tr -c 'A-Za-z0-9_' '\n' |
    awk 'tolower($0) == "harness" || tolower($0) ~ /^cmd_[a-z][a-z0-9_]*$/ { print tolower($0) }' |
    sort -u
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

# bead_scope_present <token> <file-list>
#   0 when the list carries what the token names: the entry itself, any entry
#   under it when it is a directory (`src/components/` and `src/components` both
#   answer the directory), or an entry that ends with it at a path boundary.
#   The directory case is what keeps a bead that names a directory from reading
#   as a path this tree has lost.
bead_scope_present() {
  awk -v tok="$1" '
    {
      n = length(tok)
      if ($0 == tok || index($0, tok "/") == 1) found = 1
      m = length($0)
      if (m > n && substr($0, m - n) == "/" tok) found = 1
      if (found) exit
    }
    END { exit(found ? 0 : 1) }' "$2"
}

# bead_scope_rooted <first-component> <file-list>
#   0 when the list carries that name as a directory of its own. A token whose
#   first component this tree does not have is not a path in this tree at all:
#   `work/<bead-id>`, `origin/master` and `refs/kept/<bead>` all tokenise to one
#   and none of them is a file a worker can go and edit.
bead_scope_rooted() {
  awk -v tok="$1" '
    {
      n = length(tok)
      if ($0 == tok || (length($0) > n && substr($0, 1, n + 1) == tok "/")) found = 1
      if (found) exit
    }
    END { exit(found ? 0 : 1) }' "$2"
}

# bead_scope_named_file <token>
#   0 when the token's last component is a name and an extension, which is what
#   a file the bead wants edited looks like. A glob keeps its directory and
#   loses its tail to the tokeniser (`tests/x/test-`), and a bare `.sh` is the
#   other half of one: neither is a path, and reporting them would make the
#   answer "cannot tell" for most of the queue.
bead_scope_named_file() {
  case "${1##*/}" in
    ?*.?*) return 0 ;;
    *) return 1 ;;
  esac
}

# bead_scope_deletion_log <repo-root>
#   The commits that deleted paths, as git prints them, ready for
#   bead_scope_deletions. --all because a bead written against a branch is
#   picked up after that branch landed; --no-renames because a rename otherwise
#   reads as an addition and leaves the old path looking like it was never
#   tracked; core.quotePath=false because a quoted path is one the parser would
#   read as a name with escapes in it. A checkout with no history prints nothing,
#   which is an empty list and not an error.
bead_scope_deletion_log() {
  git -C "$1" -c core.quotePath=false log --all --diff-filter=D \
    --pretty=format:'commit %h' --name-only --no-renames 2>/dev/null
}

# bead_scope_deletions
#   Reads that output on stdin and prints `<path>TAB<commit>` for the newest
#   commit that deleted each path. git prints newest first, so the first line for
#   a path is the one to keep: a file deleted, restored and deleted again is one
#   finding, and the commit a reader wants is the one that removed what they are
#   looking for.
bead_scope_deletions() {
  awk '
    /^commit / { commit = $2; next }
    NF == 0 { next }
    !($0 in seen) { seen[$0] = 1; print $0 "\t" commit }'
}

# bead_scope_deleted_for <token> <deleted-list>
#   The `<path>TAB<commit>` lines whose path the token names at a path boundary.
#   The list carries a commit column, so bead_scope_resolve cannot read it.
bead_scope_deleted_for() {
  awk -v tok="$1" '
    {
      path = $0
      sub(/\t.*/, "", path)
      n = length(tok)
      m = length(path)
      if (path == tok || (m > n && substr(path, m - n) == "/" tok)) print $0
    }' "$2"
}

# bead_scope_find <bead-json> <comments-json> <repo-files> <harness-files>
#                 <deleted-list>
#   One `<kind>TAB<path>TAB<detail>` line per token that is not simply this
#   tree's, where kind is OUTSIDE for a harness path and GONE for a path this
#   tree does not carry. A token is classified once: a harness path is not also
#   reported as gone, because a reader who is told to stop on the harness does
#   not need a second line saying the same file is not here.
#
#   The harness rule is evidence, not a vocabulary: the token resolves, at a path
#   boundary, to a file the harness checkout tracks and this repository does not.
#   The small table in bead_scope_harness_names() covers what the file lists
#   cannot - a harness name that has moved or been deleted.
#
#   The gone rule is shape, not history: an entry of the file list, a directory
#   the list holds entries under, and a path whose first component heads a
#   tracked path or a deleted one are all fine, and anything else with a file's name
#   and extension is reported. A first component that heads neither a tracked
#   path nor a deleted one is not a path here at all, which is what keeps
#   `work/<bead-id>`, `origin/master` and the `tests/x/test-` a glob leaves
#   behind out of the answer. A directory the tree deleted entirely is absent
#   from the file list; the deletion list is what says it was here. The commit
#   column is empty when no commit deleted the path: the text cannot tell a new
#   file from one another bead delivered under another name, so the verdict
#   says to grep for the behaviour first.
bead_scope_find() {
  local bead=$1 comments=$2 repo=$3 harness=$4 deleted=$5
  local text tok name what hit rest part matched path commit

  text=$(mktemp "${TMPDIR:-/tmp}/bead-scope-text.XXXXXX") || return 1
  bead_scope_text "$bead" "$comments" >"$text"

  {
    while IFS= read -r tok; do
      [ -n "$tok" ] || continue
      # A name both trees carry is this repository's: the question here is what
      # only the harness has.
      bead_scope_present "$tok" "$repo" && continue
      hit=$(bead_scope_resolve "$tok" "$harness")
      if [ -n "$hit" ]; then
        while IFS= read -r path; do
          [ -n "$path" ] || continue
          printf 'OUTSIDE\t%s\t%s\n' "$path" "the harness tracks it"
        done <<<"$hit"
        continue
      fi
      rest=$tok
      matched=0
      while [ -n "$rest" ] && [ "$matched" -eq 0 ]; do
        part=${rest%%/*}
        while IFS=$'\t' read -r name what; do
          [ -n "$name" ] || continue
          if [ "$part" = "$name" ]; then
            printf 'OUTSIDE\t%s\t%s\n' "$tok" "$what"
            matched=1
            break
          fi
        done < <(bead_scope_harness_names)
        case "$rest" in
          */*) rest=${rest#*/} ;;
          *) break ;;
        esac
      done
      [ "$matched" -eq 1 ] && continue

      bead_scope_named_file "$tok" || continue
      # ls-files has no entry under a directory the tree deleted entirely, which
      # is also true of work/ and origin/. The deletion list is the difference:
      # a commit removed something under .cursor/, and nothing removed work/.
      # Its commit column sits after a tab, so a prefix check still sees the path.
      case "$tok" in
        */*)
          bead_scope_rooted "${tok%%/*}" "$repo" \
            || bead_scope_rooted "${tok%%/*}" "$deleted" \
            || continue
          ;;
      esac
      hit=$(bead_scope_deleted_for "$tok" "$deleted")
      if [ -z "$hit" ]; then
        printf 'GONE\t%s\t\n' "$tok"
        continue
      fi
      while IFS=$'\t' read -r path commit; do
        [ -n "$path" ] || continue
        printf 'GONE\t%s\t%s\n' "$path" "$commit"
      done <<<"$hit"
    done < <(bead_scope_tokens <"$text")
  } | sort -u

  rm -f "$text"
}

# bead_scope_has_kind <kind> <findings>
#   0 when bead_scope_find reported at least one finding of that kind, so the
#   verdict asks the finding list rather than grepping a tab out of it.
bead_scope_has_kind() {
  awk -F'\t' -v kind="$1" '$1 == kind { hit = 1 } END { exit(hit ? 0 : 1) }' <<<"$2"
}

# bead_scope_verify <bead-json> <comments-json> <repo-files> <harness-files>
#                    <deleted-list>
#   0 when no token names the harness, the text does not talk about it, and
#   every path the bead names is in this tree, 3 when a token names the harness,
#   2 when the tree does not carry a path it names or the text names the harness
#   as a word or a command and no file. A bead can name a harness file and still
#   ask for a change in this tree, so the harness verdict says to land that part;
#   a path the tree lost is a different answer - the text cannot say whether the
#   bead asks for the file back or for one that never existed - so it is 2, the
#   same as a bead the tracker would not give up, and the same as a bead whose
#   fix is a harness command: neither names a file here to look at.
bead_scope_verify() {
  local bead=$1 comments=$2 repo=$3 harness=$4 deleted=$5
  local id status title found what why kind dir words

  IFS=$'\t' read -r id status title < <(
    jq -r '(if type == "array" then .[0] else . end)
      | [(.id // "?"), (.status // "?"), (.title // "")]
      | @tsv' "$bead" 2>/dev/null
  )

  printf 'bead-scope: %s (%s) %s\n' "$id" "$status" "$title"

  found=$(bead_scope_find "$bead" "$comments" "$repo" "$harness" "$deleted")

  if [ -z "$found" ]; then
    words=$(bead_scope_harness_words "$bead" "$comments" | paste -sd, -)
    if [ -n "$words" ]; then
      printf '%s: nothing in its text names a harness file, but it talks about the harness: %s.\n' \
        "$id" "${words//,/ and }"
      echo "A fix in the harness is a command and not a path, so the token scan has"
      echo "nothing to resolve and this command cannot say whether the work is here."
      echo "Read its comments for the file the fix is in: the part that is here is"
      echo "landed here, the rest cannot be. Return 2, not the 0 that reads as yes."
      return 2
    fi
    printf '%s: nothing in its text names the harness or a path this tree has lost,\n' "$id"
    echo "so its work is in this worktree."
    return 0
  fi

  while IFS=$'\t' read -r kind what why; do
    [ -n "$what" ] || continue
    if [ "$kind" = "OUTSIDE" ]; then
      printf '  OUTSIDE  %s\n' "$what"
      printf '           %s\n' "$why"
      continue
    fi
    printf '  GONE     %s\n' "$what"
    dir=${what%/*}
    if [ "$dir" = "$what" ]; then
      dir=""
    fi
    if [ -n "$why" ]; then
      printf '           deleted by %s\n' "$why"
    else
      echo "           no commit in this history deleted it, so the bead may be asking for a new file"
      echo "           grep for the behaviour first: another bead may have delivered it under"
      echo "           another name, and then the bead's text is what needs correcting"
    fi
    if [ -z "$dir" ]; then
      continue
    elif bead_scope_present "$dir" "$repo"; then
      if [ -n "$why" ]; then
        printf '           %s/ is still tracked, so look there for what replaced it\n' "$dir"
      else
        printf '           %s/ is still tracked\n' "$dir"
      fi
    else
      printf '           nothing under %s/ is tracked now\n' "$dir"
    fi
  done <<<"$found"

  if bead_scope_has_kind OUTSIDE "$found"; then
    printf '%s: it names the harness, and no commit on a work/%s branch can carry a\n' "$id" "$id"
    echo "change there. Land what is in this worktree if the bead has any, record the"
    echo "rest in a bd comment, and stop: the harness is read here and not edited."
    if bead_scope_has_kind GONE "$found"; then
      echo "It also names a path this tree does not have; read the GONE lines before"
      echo "you plan."
    fi
    return 3
  fi

  printf '%s: it names a path this tree does not have, so this command cannot say\n' "$id"
  echo "whether the work is here. A deleted path means the bead was written against a"
  echo "tree that had it: do the work where the code lives now, or say in the bead"
  echo "comment why it should not be done. A path no commit deleted may be a file the"
  echo "bead asks you to add, and it may be one another bead delivered under another"
  echo "name, which makes the bead's text stale and the file not missing: grep for the"
  echo "behaviour first. Return 2: the caller looks, because neither reading is an"
  echo "answer to what is in this worktree."
  return 2
}
