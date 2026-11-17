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
#   - Only a line whose first word is a program that reads and exits: rg, grep,
#     `man -w`, or one of the git subverbs in GIT_READ_ONLY below. The rest of
#     this tree's documented commands start a session, build, mutate the tree or
#     take minutes (`make format`, `--update-baseline`, `scripts/perf-test.sh`),
#     and a gate runs on every commit.
#   - Only a line this file can turn into an argument list by itself. The
#     command is never handed to a shell: a shell is what turns `$'--pre=x'`,
#     `--open-files-in-page{r,}=x`, `-nOx` and `${1:+--pre=x}` into words this
#     file never looked at, and one of those words running a program is how a
#     document gets a command into every commit. A line is split here, with the
#     quoting rules a shell would apply, and a line using anything else - a
#     glob, a brace, a redirection, a pipe, a command substitution, an ANSI-C
#     quote, an expansion other than `$name` and `${name}` - is printed as not
#     run instead.
#   - Only flags on the list below. This is the cap: the list is the whole of
#     what this gate hands to a program, so a flag that runs another program (rg
#     --pre, git grep -O/--open-files-in-pager, git --ext-diff) is not run, in
#     any spelling, and neither is a flag nobody has looked at. A denylist
#     cannot do this: the set of ways a program can be told to run one is open,
#     and each program spells its own flags, abbreviations and short-option
#     clusters its own way. Adding a flag here is a decision someone makes once.
#   - Not a command naming a variable this shell does not have. $LIBXCB_SRC is
#     the dev shell's, so a run outside the shell re-enters it the way
#     scripts/run-hook.sh does. A variable nothing sets is printed with the
#     command it left unrun, rather than run against a path that collapsed. A
#     `$name` inside single quotes is the program's argument, not an expansion.
#   - Not a command naming $1 or another parameter of the gate's own command
#     line. The hook runs this script with no arguments; a by-hand run passes a
#     document. The document's reader has a different command line from either.
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
# and exits. GIT_READ_ONLY is the subverbs only: checkout, reset, stash and the
# rest write the tree.
READ_ONLY_CMDS='rg grep man'
GIT_READ_ONLY='blame cat-file describe diff for-each-ref grep log ls-files reflog rev-parse show shortlog status'

# The flags this gate hands to a program, per program. None of them names
# another program or writes a file; each was read in the program's own manual
# before it was written here. `--` is on every list because it is how a document
# passes a pattern that starts with a dash, and everything after it is an
# operand.
RG_FLAGS='-- -n -i -l -c -w -F -v -uu --hidden --no-ignore --files'
GREP_FLAGS='-- -n -i -l -c -w -F -v -r -R -I'
MAN_FLAGS='-- -w'
GIT_FLAGS='-- --oneline -p -1 --stat --name-only'

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

# doc_command_lines <file> - one line per fenced command, as the document wrote
# it, tab-separated from its line number.
#
# Fence state, a trailing comment outside quotes, and the placeholder rule are
# one pass, so the rules above are applied in the order they are written. The
# rest - splitting, the program allowlist, the flag allowlist - is in bash,
# because what a line means depends on quoting this awk pass does not model.
doc_command_lines() {
  awk '
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
      if (cmd ~ /<[^>]*>/) next
      printf "%d\t%s\n", NR, cmd
    }
  ' "$1"
}

# list_has <word> <space-separated list>
list_has() {
  case " $2 " in
    *" $1 "*) return 0 ;;
  esac
  return 1
}

# skip_line <first word> <second word> - 0 when the line is not one this gate
# runs and is passed over without a word: a program outside the allowlist, a git
# subverb that writes, or a man without -w. `make test && make test-x` and a
# session script are ordinary lines in these documents, not findings.
skip_line() {
  local prog=$1 sub=${2:-}
  if [ "$prog" = "git" ]; then
    ! list_has "$sub" "$GIT_READ_ONLY"
    return
  fi
  if ! list_has "$prog" "$READ_ONLY_CMDS"; then
    return 0
  fi
  if [ "$prog" = "man" ] && [ "$sub" != "-w" ]; then
    return 0
  fi
  return 1
}

# expand_ref <line> <index of the $> - sets REF_WORD, REF_NEXT and REF_ERR from
# one `$name`, `${name}` or shell parameter. REF_ERR says why the reference is
# not one this gate substitutes.
expand_ref() {
  local s=$1 i=$(($2 + 1)) c=${1:$2+1:1} n=${#1} j name
  REF_WORD=""
  REF_ERR=""
  case "$c" in
    '{')
      j=$((i + 1))
      name=""
      while [ "$j" -lt "$n" ] && [[ ${s:j:1} == [A-Za-z0-9_] ]]; do
        name+=${s:j:1}
        j=$((j + 1))
      done
      if [ -z "$name" ] || [ "${s:j:1}" != "}" ]; then
        REF_ERR="a shell expansion this gate does not model"
        return 1
      fi
      REF_NEXT=$((j + 1))
      ;;
    [A-Za-z_]*)
      j=$i
      name=""
      while [ "$j" -lt "$n" ] && [[ ${s:j:1} == [A-Za-z0-9_] ]]; do
        name+=${s:j:1}
        j=$((j + 1))
      done
      REF_NEXT=$j
      ;;
    *)
      if [[ $c == [0-9@*#?!$-] ]]; then
        REF_ERR="\$$c is a parameter of this gate's command line, and the document's reader has a different one"
      else
        REF_ERR="a shell expansion this gate does not model"
      fi
      return 1
      ;;
  esac
  if [ -z "${!name:-}" ]; then
    REF_ERR="\$$name is not set, and nothing in this shell sets it"
    return 1
  fi
  REF_WORD=${!name}
  return 0
}

# split_cmd <line> - one word per line, exactly as a shell would hand them to
# the program, in the order the program sees them. Returns 1 and sets
# SPLIT_REASON for a line this gate will not model.
split_cmd() {
  local s=$1 i=0 n=${#1} c w="" inword=0 quote=""
  SPLIT_REASON=""
  while [ "$i" -lt "$n" ]; do
    c=${s:i:1}
    if [ -n "$quote" ]; then
      if [ "$c" = "$quote" ]; then
        quote=""
      elif [ "$quote" = '"' ] && [ "$c" = "\\" ]; then
        i=$((i + 1))
        w+=${s:i:1}
      elif [ "$quote" = '"' ] && [ "$c" = '$' ]; then
        expand_ref "$s" "$i" || {
          SPLIT_REASON=$REF_ERR
          return 1
        }
        w+=$REF_WORD
        i=$REF_NEXT
        continue
      else
        w+=$c
      fi
      i=$((i + 1))
      continue
    fi
    case "$c" in
      ' ' | '	')
        if [ "$inword" = 1 ]; then
          printf '%s\n' "$w"
          w=""
          inword=0
        fi
        ;;
      "'" | '"')
        quote=$c
        inword=1
        ;;
      "\\")
        i=$((i + 1))
        w+=${s:i:1}
        inword=1
        ;;
      '$')
        expand_ref "$s" "$i" || {
          SPLIT_REASON=$REF_ERR
          return 1
        }
        w+=$REF_WORD
        inword=1
        i=$REF_NEXT
        continue
        ;;
      '|' | '&' | ';' | '<' | '>' | '`' | '{' | '}' | '*' | '?' | '[' | ']' | '~')
        SPLIT_REASON="$c is shell syntax this gate does not model"
        return 1
        ;;
      *)
        w+=$c
        inword=1
        ;;
    esac
    i=$((i + 1))
  done
  if [ -n "$quote" ]; then
    SPLIT_REASON="a quote that is never closed"
    return 1
  fi
  if [ "$inword" = 1 ]; then
    printf '%s\n' "$w"
  fi
  return 0
}

# text_skips <line> - the same question for a line this gate cannot split,
# where all it has is the text: the first two words, quoted or not.
text_skips() {
  local first second rest
  read -r first second rest <<<"$1"
  skip_line "$first" "$second"
}

RAN=0
FAILED=0
UNRUN=0
DOCS_WITH_COMMANDS=()
read_docs=0
WORDS_FILE="${TMPDIR:-/tmp}/wm-doc-commands.$$"
trap 'rm -f "$WORDS_FILE"' EXIT

not_run() { # not_run <file> <line> <command> <reason>...
  UNRUN=$((UNRUN + 1))
  printf '  NOT RUN  %s:%s\n' "$1" "$2"
  printf '           %s\n' "$3"
  shift 3
  local reason
  for reason in "$@"; do
    printf '           %s\n' "$reason"
  done
}

for f in "${DOCS[@]}"; do
  [ -f "$f" ] || continue
  read_docs=$((read_docs + 1))
  contributed=0

  command_lines=$(doc_command_lines "$f") || {
    echo "check-doc-commands: could not read $f for commands."
    echo "The scanner that finds them failed, so nothing in this file was checked"
    echo "and this run is not a pass."
    exit 1
  }

  while IFS= read -r entry; do
    [ -n "$entry" ] || continue
    lineno=${entry%%$'\t'*}
    cmd=${entry#*$'\t'}

    if ! split_cmd "$cmd" >"$WORDS_FILE"; then
      # A line this gate will not model is only worth reporting when it looks
      # like one of its own commands. `make test && make test-x` fails to split
      # on the `&&`, and it is the allowlist that keeps it out, not the split.
      if text_skips "$cmd"; then
        continue
      fi
      not_run "$f" "$lineno" "$cmd" "$SPLIT_REASON"
      contributed=1
      continue
    fi
    mapfile -t words <"$WORDS_FILE"
    [ "${#words[@]}" -gt 0 ] || continue

    if skip_line "${words[0]}" "${words[1]:-}"; then
      # A program this gate does not run is the documented allowlist, not a
      # finding: `make format` and a session script are ordinary lines here.
      continue
    fi
    contributed=1

    start=1
    allowed=$RG_FLAGS
    case "${words[0]}" in
      grep) allowed=$GREP_FLAGS ;;
      man) allowed=$MAN_FLAGS ;;
      git)
        allowed=$GIT_FLAGS
        start=2
        ;;
    esac

    operands=0
    bad=""
    for ((i = start; i < ${#words[@]}; i++)); do
      w=${words[i]}
      if [ "$operands" = 1 ]; then
        continue
      fi
      if [ "$w" = "--" ]; then
        operands=1
        continue
      fi
      case "$w" in
        -*) ;;
        *) continue ;;
      esac
      # `--flag=value` is the flag and its value; the value is the program's
      # to read, so only the name is checked.
      case "$w" in
        *=*) key=${w%%=*} ;;
        *) key=$w ;;
      esac
      if ! list_has "$key" "$allowed"; then
        bad=$w
        break
      fi
    done
    if [ -n "$bad" ]; then
      not_run "$f" "$lineno" "$cmd" \
        "$bad is not a flag this gate runs for ${words[0]}, and a flag it has" \
        "not seen is a flag nobody has checked"
      continue
    fi

    # The argument list built above is what runs. No shell, so no word reaches
    # the program that this gate did not read.
    output=$("${words[@]}" </dev/null 2>&1)
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
  done <<<"$command_lines"

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
