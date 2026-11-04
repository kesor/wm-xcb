# shellcheck shell=bash
#
# lib/async-scan.sh - the source analysis behind scripts/check-async.sh.
#
# Sourced by scripts/check-async.sh, which asks it two questions about a text
# hit, and by tests/x/test-100-async-scan.sh, which feeds it small sources
# instead of the tree.
#
# A grep can say that a line calls something that waits. It cannot say who can
# reach that line, and that is what decides whether an `allow-blocking` comment
# is true: the reason is a claim about when the code runs, and a helper is not
# startup-only when an XCB handler calls it too. keybinding_numlock_mask used a
# reply under a "startup only" waiver, keybinding_grab_keys called it, and the
# MappingNotify handler called that: the gate reported the line as waived while
# the keymap changed and the WM waited for a reply.
#
# async_scan_init reads the sources once and answers from what it read:
#
#   - the functions the event loop calls, which are the handlers named in an
#     xcb_handler_register() call in the tree, plus everything those reach by a
#     call written in the same tree;
#   - the line range of every function definition, to name the function a hit
#     sits in.
#
# Both are read off the source text, so the limits are worth knowing. A call is
# a name followed by '(': comments and strings are not stripped, so a name
# mentioned inside a function body counts as a call from it. That direction errs
# toward refusing a waiver, and the reader sees the refusal and the name it came
# from. A callback stored in a struct field and called through the field is not
# followed, because the source does not say when it runs; xcb_handler_register
# is the tree's way of handing a function to the loop, and it is the one that is
# followed.

# The state async_scan_init fills. Both are read by the functions below.
declare -A ASYNC_LOOP_CALLABLE=()
declare -a ASYNC_RANGES=()

# _async_analyze <file.c>... - one record per line on stdout:
#   range      <file> <name> <first line> <last line>
#   callable   <name>
_async_analyze() {
  awk '
    FNR == 1 { cur = ""; cand = ""; inreg = 0; regtext = "" }

    # The handler is the last name before the closing paren of the call, however
    # the call is wrapped. The definition of xcb_handler_register is at column
    # 0, so it is not inside a function and is not read as a registration.
    function flush_registration(text, t, parts, n, i) {
      t = text
      sub(/^[^(]*\(/, "", t)
      sub(/\)[^()]*$/, "", t)
      n = split(t, parts, /[^A-Za-z0-9_]+/)
      for (i = n; i >= 1; i--)
        if (parts[i] != "") { handlers[parts[i]] = 1; return }
    }

    # clang-format leaves a comment on the closing line (`); /* ... */` or
    # `); // ...`, with spaces before it). A parenthesis in that comment would
    # be the last name if it stayed in the text the flush reads.
    function without_trailing_comment(s) {
      sub(/[ \t]*\/\/.*/, "", s)
      sub(/[ \t]*\/\*.*/, "", s)
      return s
    }

    {
      line = $0

      if (cur != "" && index(line, "xcb_handler_register") > 0 && line ~ /\(/) {
        inreg = 1
        regtext = ""
      }
      if (inreg) {
        regtext = regtext " " without_trailing_comment(line)
        if (line ~ /\);[ \t]*(\/\/.*|\/\*.*)?$/) {
          flush_registration(regtext)
          inreg = 0
        }
      }

      if (cur != "") {
        if (line ~ /^}/) {
          printf "range\t%s\t%s\t%d\t%d\n", FILENAME, cur, start, FNR
          cur = ""
          next
        }
        rest = line
        while (match(rest, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
          callee = substr(rest, RSTART, RLENGTH)
          sub(/[ \t]*\($/, "", callee)
          edges[cur SUBSEP callee] = 1
          rest = substr(rest, RSTART + RLENGTH)
        }
        next
      }

      # A definition puts its name at column 0 and opens the body on a later
      # line. A name followed by more indented lines is a wrapped parameter
      # list; anything else at column 0 is a declaration or an attribute, and
      # the line is read again as a possible definition.
      again = 1
      while (again) {
        again = 0
        if (cand == "") {
          if (line ~ /^[A-Za-z_][A-Za-z0-9_]*\(/) {
            cand = line
            sub(/\(.*/, "", cand)
            start = FNR
          }
          break
        }
        if (line ~ /^\{/) {
          cur = cand
          defined[cand] = 1
          cand = ""
          break
        }
        if (line ~ /^[ \t]/) break
        cand = ""
        again = 1
      }
    }

    END {
      head = 1
      tail = 0
      for (name in handlers) {
        if (name in defined && !(name in reached)) {
          reached[name] = 1
          queue[++tail] = name
        }
      }
      while (head <= tail) {
        caller = queue[head++]
        for (edge in edges) {
          split(edge, pair, SUBSEP)
          if (pair[1] != caller) continue
          callee = pair[2]
          if (!(callee in defined) || (callee in reached)) continue
          reached[callee] = 1
          queue[++tail] = callee
        }
      }
      for (name in reached) print "callable\t" name
    }
  ' "$@"
}

# async_scan_init <file.c>... - read the sources, then answer from them.
async_scan_init() {
  local record
  ASYNC_LOOP_CALLABLE=()
  ASYNC_RANGES=()
  while IFS= read -r record; do
    case "$record" in
      callable$'\t'*) ASYNC_LOOP_CALLABLE["${record#callable$'\t'}"]=1 ;;
      range$'\t'*)
        record=${record#range$'\t'}
        ASYNC_RANGES+=("${record//$'\t'/|}")
        ;;
    esac
  done < <(_async_analyze "$@")
}

# async_loop_callable <function> - succeeds when the event loop can reach it.
async_loop_callable() {
  [ -n "${ASYNC_LOOP_CALLABLE[$1]:-}" ]
}

# async_enclosing_function <file> <line> - the function whose body holds the
# line, or nothing when no definition in the tree does.
async_enclosing_function() {
  local want_file=$1 want_line=$2 range file name first last
  for range in "${ASYNC_RANGES[@]}"; do
    IFS='|' read -r file name first last <<<"$range"
    if [ "$file" = "$want_file" ] && [ "$want_line" -ge "$first" ] && [ "$want_line" -le "$last" ]; then
      printf '%s' "$name"
      return 0
    fi
  done
  return 0
}

# async_waiver_verdict <file> <line> - what the allow-blocking comment on or just
# above that line is worth:
#
#   none        no comment covers the line
#   unreasoned  a comment covers it and says nothing after allow-blocking:
#   waived      a comment covers it and the event loop cannot reach the function
#   refused     a comment covers it and the event loop can reach the function:
#               the reason claims the wait happens before the loop, and a
#               handler that reaches it runs inside the loop
async_waiver_verdict() {
  local file=$1 line=$2 start context enclosing
  start=$((line - 2))
  [ "$start" -lt 1 ] && start=1
  context=$(sed -n "${start},${line}p" "$file")
  if printf '%s' "$context" | grep -qE 'allow-blocking:[[:space:]]*[^[:space:]*]'; then
    enclosing=$(async_enclosing_function "$file" "$line")
    if [ -n "$enclosing" ] && async_loop_callable "$enclosing"; then
      printf 'refused'
    else
      printf 'waived'
    fi
    return 0
  fi
  if printf '%s' "$context" | grep -q 'allow-blocking:'; then
    printf 'unreasoned'
    return 0
  fi
  printf 'none'
}

# async_strip_comments <file> - the file with comment text and string contents
# blanked, one output line per input line so a reported line number is the line
# the reader opens.
#
# scripts/check-async.sh runs the rules over this instead of the source text.
# The per-line comment filter it replaced only hid a line that *began* with a
# comment marker, so a /* ... */ block whose continuation line started with
# waitpid( was reported as a blocking wait and the author had to reword the
# sentence to get past the gate. A call name inside a string literal fooled it
# the same way. Quote characters and tabs are kept so a printed line still
# lines up with the source.
async_strip_comments() {
  awk '
    BEGIN { quotechar = sprintf("%c", 39) }
    {
      line = $0
      out = ""
      i = 1
      n = length(line)
      # A // comment ending in a backslash runs on into the next line.
      if (in_line_comment) {
        in_line_comment = (line ~ /\\$/)
        print ""
        next
      }
      while (i <= n) {
        c = substr(line, i, 1)
        if (in_block) {
          if (c == "*" && substr(line, i + 1, 1) == "/") {
            in_block = 0
            out = out "  "
            i += 2
          } else {
            out = out (c == "\t" ? "\t" : " ")
            i++
          }
          continue
        }
        if (in_quote != "") {
          if (c == "\\") {
            out = out "  "
            i += 2
            continue
          }
          if (c == in_quote) {
            in_quote = ""
            out = out c
            i++
            continue
          }
          out = out (c == "\t" ? "\t" : " ")
          i++
          continue
        }
        if (c == "/" && substr(line, i + 1, 1) == "*") {
          in_block = 1
          out = out "  "
          i += 2
          continue
        }
        if (c == "/" && substr(line, i + 1, 1) == "/") {
          in_line_comment = (line ~ /\\$/)
          break
        }
        if (c == "\"" || c == quotechar) {
          in_quote = c
          out = out c
          i++
          continue
        }
        out = out c
        i++
      }
      print out
    }
  ' "$1"
}
