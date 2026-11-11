#!/usr/bin/env bash
#
# check-doc-commands.sh - run the commands a document prints, so one that
# answers nothing fails here instead of teaching the next reader.
#
# Usage:
#   scripts/check-doc-commands.sh [file...]    # scan these documents, not the tree
#   scripts/check-doc-commands.sh -v|--verbose # also name every document read
#
# A document prints a command so the reader gets the answer without trusting
# the prose. Nothing ran those commands. docs/TESTING.md carried
#
#   rg -n 'REQUEST_CHECKED' "$LIBXCB_SRC/src/xcb_out.c"
#
# for a macro defined in src/xcbext.h: the command exited 1 and printed nothing,
# so the document named a file that holds nothing of the kind, and it stood
# until a reviewer read the libxcb headers by hand. The author had counted lines
# in the two files; the command it wrote was never run.
#
# What is read, and what is left alone:
#
#   - Only a line inside a ```bash, ```sh or ```shell fence. An unlabelled fence
#     in this tree holds program output and C, and running that is a guess.
#   - Only a line that is one simple command: no pipe, `;`, `&&`, `&`,
#     redirection, `$(...)`, backtick or continuation. A gate reports an exit
#     status, and a compound line has more than one. A `|`, `<` or `\` inside
#     quotes is the program's argument, not shell syntax, and the line is run.
#     A backtick or `$(` inside double quotes is still shell syntax.
#   - Not a line carrying <placeholder>, which the reader substitutes.
#   - The command's standard input is closed. This loop's standard input is the
#     list of candidates still to run; a command that reads it consumes the next
#     candidate and the gate reports that command's exit instead.
#   - Only a first word that reads and exits: rg, grep, `man -w`, or one of the
#     git subverbs in GIT_READ_ONLY below. The rest of this tree's documented
#     commands start a session, build, mutate the tree or take minutes (`make
#     format`, `--update-baseline`, `scripts/xephyr-test.sh`), and a gate runs on
#     every commit.
#   - Not a line whose arguments hand that program another one to run: rg
#     --pre, git grep --open-files-in-pager, git's --ext-diff, which runs
#     $GIT_EXTERNAL_DIFF. Those programs read and exit only while no argument
#     names a program, and a gate that runs a document's program is a gate with
#     an arbitrary command in it.
#   - Not a command naming a variable this shell does not have. $LIBXCB_SRC is
#     the dev shell's, so a run outside the shell re-enters it the way
#     scripts/run-hook.sh does. A variable nothing sets is printed with the
#     command it left unrun, rather than run against a path that collapsed. A
#     `$name` inside single quotes is the program's argument, not an expansion.
#   - Not a command naming $1 or another parameter of the gate's own command
#     line. The hook runs this script with no arguments, where `set -u` makes
#     "$1" an error; a by-hand run passes a document, and the same line then
#     searches that document and can exit 0. The document's reader has a
#     different command line from either.
#
# A command that exits non-zero fails the check. It is printed with its
# document, its line and its output; empty output is printed as the exit status
# alone, because "printed nothing" is what the defect above looked like.
#
# Exit status: 0 when every command that ran exited 0, 1 otherwise.

set -uo pipefail

SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

# A document may print these and nothing else: each reads what it is pointed at
# and exits, so the answer is the same twice and costs no seconds. GIT_READ_ONLY
# is the subverbs only: checkout, reset, stash and the rest write the tree.
READ_ONLY_CMDS='rg grep man'
GIT_READ_ONLY='blame cat-file describe diff for-each-ref grep log ls-files reflog rev-parse show shortlog status'

VERBOSE=0
FILES=()
for arg in "$@"; do
  case "$arg" in
    -v | --verbose) VERBOSE=1 ;;
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *) FILES+=("$arg") ;;
  esac
done

# $LIBXCB_SRC comes from the dev shell, and a document's reader gets it from
# `nix develop` too. Outside that shell every "$LIBXCB_SRC/src/..." collapses to
# /src/..., rg reports "no such file", and the gate would fail on the shell it
# was started from. Re-enter the shell once, the way scripts/run-hook.sh does.
if [ -z "${LIBXCB_SRC:-}" ] && [ -z "${WM_DOC_COMMANDS_IN_SHELL:-}" ] &&
  command -v nix >/dev/null 2>&1 && [ -f "$ROOT_DIR/flake.nix" ]; then
  echo "check-doc-commands: the dev shell's variables are not on PATH, re-entering it" >&2
  exec nix develop "$ROOT_DIR" --command env WM_DOC_COMMANDS_IN_SHELL=1 "$SELF" "$@"
fi

if [ "${#FILES[@]}" -gt 0 ]; then
  DOCS=("${FILES[@]}")
else
  mapfile -t DOCS < <(
    {
      git ls-files '*.md'
      echo 'AGENTS.md'
    } | sort -u |
      grep -v '^docs/archive/' |
      grep -v '^vendor/'
  )
fi

# doc_candidates <file> - `<line><tab><command>` for each line this gate runs.
#
# Fence state, a trailing comment outside quotes, and the run-or-skip decision
# are one pass, so the rules above are applied in the order they are written.
doc_candidates() {
  awk -v read_only="$READ_ONLY_CMDS" -v git_read_only="$GIT_READ_ONLY" '
    function trim(s) {
      sub(/^[ \t]+/, "", s)
      sub(/[ \t]+$/, "", s)
      return s
    }

    # A `#` starts a comment at a word boundary outside quotes. Tracking both
    # quote characters matters here: `rg -n "a #b" file` keeps its pattern, and
    # `rg -n '"'"'x'"'"' file  # why` loses the note.
    function unquoted(line,   i, n, c, sq, dq, prev) {
      n = length(line)
      sq = 0
      dq = 0
      prev = " "
      for (i = 1; i <= n; i++) {
        c = substr(line, i, 1)
        if (sq) {
          if (c == "\047") sq = 0
        } else if (dq) {
          if (c == "\"") dq = 0
        } else if (c == "\047") {
          sq = 1
        } else if (c == "\"") {
          dq = 1
        } else if (c == "#" && (prev == " " || prev == "\t")) {
          return substr(line, 1, i - 1)
        }
        prev = c
      }
      return line
    }

    # 1 when the line is more than one command. A `|` inside quotes is the
    # program argument: skipping the line would leave that command unchecked,
    # which is the failure this gate exists to catch. A backtick or `$(` still
    # counts inside double quotes, because the shell runs it there.
    function shell_meta(line,   i, n, c, sq, dq) {
      n = length(line)
      sq = 0
      dq = 0
      for (i = 1; i <= n; i++) {
        c = substr(line, i, 1)
        if (sq) {
          if (c == "\047") sq = 0
          continue
        }
        if (dq) {
          if (c == "\\") {
            i++
            continue
          }
          if (c == "\"") {
            dq = 0
            continue
          }
          if (c == "`") return 1
          if (c == "$" && substr(line, i + 1, 1) == "(") return 1
          continue
        }
        if (c == "\\") {
          if (i == n) return 1
          i++
          continue
        }
        if (c == "\047") {
          sq = 1
          continue
        }
        if (c == "\"") {
          dq = 1
          continue
        }
        if (c == "|" || c == "&" || c == ";" || c == "<" || c == ">" || c == "`") return 1
        if (c == "$" && substr(line, i + 1, 1) == "(") return 1
      }
      return 0
    }

    function listed(word, list,   n, i, parts) {
      n = split(list, parts, " ")
      for (i = 1; i <= n; i++) if (parts[i] == word) return 1
      return 0
    }

    # The shell removes its quotes before the program sees them, so a quoted
    # `--pre=/tmp/x` reaches rg as that flag: the word is unquoted the way the
    # shell would before the flag is recognised.
    function unquote(s,   i, c, out) {
      out = ""
      for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c != "\047" && c != "\"") out = out c
      }
      return out
    }

    # 1 when an argument hands the allowlisted program another one to run.
    function runs_program(w, nw,   i, verb, arg) {
      verb = (w[1] == "git") ? w[2] : ""
      for (i = 2; i <= nw; i++) {
        arg = unquote(w[i])
        if (w[1] == "rg" && (arg == "--pre" || arg ~ /^--pre=/)) return 1
        if (verb == "grep" && (arg == "-O" || arg ~ /^-O./ ||
            arg == "--open-files-in-pager" || arg ~ /^--open-files-in-pager=/)) return 1
        if (w[1] == "git" && arg == "--ext-diff") return 1
      }
      return 0
    }

    /^[ \t]*```/ {
      if (fenced) { fenced = 0; next }
      info = $0
      sub(/^[ \t]*```/, "", info)
      sub(/[ \t].*$/, "", info)
      fenced = (info == "bash" || info == "sh" || info == "shell")
      next
    }

    !fenced { next }

    {
      cmd = trim(unquoted($0))
      if (cmd == "") next
      if (shell_meta(cmd)) next
      nw = split(cmd, w, /[ \t]+/)
      if (w[1] ~ /=/) next
      if (w[1] == "git") {
        if (w[2] == "" || w[2] ~ /^-/) next
        if (!listed(w[2], git_read_only)) next
      } else if (w[1] == "man") {
        if (w[2] != "-w") next
      } else if (!listed(w[1], read_only)) {
        next
      }
      reason = ""
      if (runs_program(w, nw)) reason = "program"
      printf "%d\t%s\t%s\n", NR, reason, cmd
    }
  ' "$1"
}

# doc_missing <command> - one line per token the gate cannot resolve, as
# `var:NAME` for a variable this shell does not set and `param:$1` for a
# parameter of the gate's own command line. A `$name` inside single quotes is
# the program's argument, not an expansion; treating it as unset would skip a
# command the reader would run.
doc_missing() {
  local name ref
  printf '%s\n' "$1" |
    awk '
      function emit(line,   i, n, c, sq, dq, name, j) {
        n = length(line)
        i = 1
        sq = 0
        dq = 0
        while (i <= n) {
          c = substr(line, i, 1)
          if (sq) {
            if (c == "\047") sq = 0
            i++
            continue
          }
          if (c == "\\") {
            i += 2
            continue
          }
          if (dq && c == "\"") {
            dq = 0
            i++
            continue
          }
          if (!dq && c == "\047") {
            sq = 1
            i++
            continue
          }
          if (!dq && c == "\"") {
            dq = 1
            i++
            continue
          }
          if (c == "$") {
            c = substr(line, i + 1, 1)
            if (c == "{") {
              name = ""
              j = i + 2
              if (substr(line, j, 1) ~ /[A-Za-z_]/) {
                while (j <= n && substr(line, j, 1) ~ /[A-Za-z0-9_]/) {
                  name = name substr(line, j, 1)
                  j++
                }
                if (substr(line, j, 1) == "}") print "var:" name
              } else if (substr(line, j, 1) ~ /[0-9@*#?!$-]/ &&
                substr(line, j + 1, 1) == "}") {
                print "param:$" substr(line, j, 1)
              }
            } else if (c ~ /[A-Za-z_]/) {
              name = ""
              j = i + 1
              while (j <= n && substr(line, j, 1) ~ /[A-Za-z0-9_]/) {
                name = name substr(line, j, 1)
                j++
              }
              print "var:" name
              i = j
              continue
            } else if (c ~ /[0-9@*#?!$-]/) {
              print "param:$" c
              i += 2
              continue
            }
          }
          i++
        }
      }
      { emit($0) }
    ' |
    while IFS= read -r ref; do
      [ -n "$ref" ] || continue
      case "$ref" in
        param:*) printf '%s\n' "$ref" ;;
        var:*)
          name=${ref#var:}
          [ -n "${!name:-}" ] || printf '%s\n' "$ref"
          ;;
      esac
    done
}

RAN=0
FAILED=0
UNRUN=0
DOCS_WITH_COMMANDS=()
read_docs=0

for f in "${DOCS[@]}"; do
  [ -f "$f" ] || continue
  read_docs=$((read_docs + 1))
  contributed=0

  candidates=$(doc_candidates "$f") || {
    echo "check-doc-commands: could not read $f for commands."
    echo "The scanner that finds them failed, so nothing in this file was checked"
    echo "and this run is not a pass."
    exit 1
  }

  while IFS= read -r entry; do
    [ -n "$entry" ] || continue
    lineno=${entry%%$'\t'*}
    entry=${entry#*$'\t'}
    reason=${entry%%$'\t'*}
    cmd=${entry#*$'\t'}
    contributed=1

    if [ "$reason" = "program" ]; then
      UNRUN=$((UNRUN + 1))
      printf '  NOT RUN  %s:%s\n' "$f" "$lineno"
      printf '           %s\n' "$cmd"
      printf '           an argument names another program for %s to run, and\n' "${cmd%% *}"
      printf '           this gate runs only the command itself\n'
      continue
    fi

    missing=$(doc_missing "$cmd" | sort -u)
    if [ -n "$missing" ]; then
      UNRUN=$((UNRUN + 1))
      printf '  NOT RUN  %s:%s\n' "$f" "$lineno"
      printf '           %s\n' "$cmd"
      while IFS= read -r ref; do
        case "$ref" in
          param:*)
            printf '           %s is a parameter of this gate'\''s command line, and\n' \
              "${ref#param:}"
            printf '           the document'\''s reader has a different one\n'
            ;;
          var:*)
            printf '           %s is not set, and nothing in this shell sets it\n' \
              "\$${ref#var:}"
            ;;
        esac
      done <<<"$missing"
      continue
    fi

    # The while loop's stdin is the candidate list. A command that reads stdin
    # would consume the next candidate and the gate would report that exit.
    output=$(eval "$cmd" </dev/null 2>&1)
    status=$?
    if [ "$status" -eq 0 ]; then
      RAN=$((RAN + 1))
      continue
    fi

    FAILED=$((FAILED + 1))
    printf '  FAILED   %s:%s\n' "$f" "$lineno"
    printf '           %s\n' "$cmd"
    if [ -n "$output" ]; then
      printf '%s\n' "$output" | head -5 | sed 's/^/           /'
    else
      printf '           exit %s, and it printed nothing\n' "$status"
    fi
  done <<<"$candidates"

  if [ "$contributed" -eq 1 ]; then
    DOCS_WITH_COMMANDS+=("$f")
  fi
done

if [ "$read_docs" -eq 0 ]; then
  echo "check-doc-commands: no document was read. 'git ls-files' found none"
  echo "and no file argument named one, so nothing was checked and this is not a pass."
  exit 1
fi

if [ "$FAILED" -gt 0 ]; then
  echo
  echo "check-doc-commands: $FAILED command(s) printed by a document exit non-zero."
  echo "The reader gets no answer from them, so point the command at the file that"
  echo "holds the symbol, or drop it. Only read-only commands are run; see the"
  echo "allowlist in the header of this script."
  exit 1
fi

summary="check-doc-commands: $RAN command(s) ran clean"
if [ "$UNRUN" -gt 0 ]; then
  summary="$summary, $UNRUN left unrun and printed above"
fi
if [ "${#DOCS_WITH_COMMANDS[@]}" -gt 0 ]; then
  list=$(printf '%s\n' "${DOCS_WITH_COMMANDS[@]}" | paste -sd, - | sed 's/,/, /g')
  summary="$summary, from $list"
fi
printf '%s\n' "$summary"
printf 'check-doc-commands: %d document(s) read for fenced commands\n' "$read_docs"
if [ "$VERBOSE" -eq 1 ]; then
  for f in "${DOCS[@]}"; do
    [ -f "$f" ] && printf '  %s\n' "$f"
  done
fi
exit 0
