# shellcheck shell=bash
#
# lib/sanitize-x-report.sh - judge one recorded sanitize-x run, and keep the
# caller's environment out of what that run reports.
#
# Sourced by scripts/check-sanitize-x.sh, which starts the harness and then asks
# this what came out of it, and by tests/x/test-95-sanitize-x-report.sh, which
# feeds it recorded output instead of starting a nested X server.
#
# ROOT_DIR must be set. A site is keyed by a path relative to it, because a
# frame in a library outside the repository is not a site here.
#
# The gate exists because a stack-buffer-overflow in the map path survived every
# other gate. A gate that can be silenced answers the same as one that found
# nothing, so three shapes are refused here:
#
#   - a harness line naming a variant other than release-sanitize: an
#     uninstrumented binary writes a clean log, and nothing in the log says so;
#   - a suppression file, or a report redirected away from the log. Both turn a
#     real report into silence, so sanitize_x_scrub_options removes those keys
#     from the caller's sanitizer options before the harness starts;
#   - a baseline with entries and a log with no report: either the fixes landed
#     without the baseline update, or the sanitizer never ran. The log cannot
#     say which, so the gate does not guess.

# sanitize_x_scrub_options <environment variable name>
#   Drops suppressions= and log_path= from the named sanitizer option string and
#   writes the result back. The runtime splits that string on colon, comma, and
#   whitespace, so a comma-only split leaves either key in place when another
#   key precedes it. Either key turns a real report into silence, and appending
#   detect_leaks=1 does not undo that: those two are not flags the forced suffix
#   can override. Surviving keys are rejoined with commas, which the runtime
#   accepts as a separator, so the caller can then append the keys the gate forces.
sanitize_x_scrub_options() {
  local name=$1 value items=() kept=() item
  value=${!name-}
  IFS=$' \t\n:,' read -r -a items <<<"$value"
  for item in "${items[@]}"; do
    case "$item" in
      '' | suppressions=* | log_path=*) continue ;;
    esac
    kept+=("$item")
  done
  if [ "${#kept[@]}" -eq 0 ]; then
    printf -v "$name" '%s' ''
  else
    local IFS=,
    printf -v "$name" '%s' "${kept[*]}"
  fi
  export "${name?}"
}

# sanitize_x_trust <harness-output>
#   Prints the log path the harness named, and returns non-zero with the reason
#   on stderr when the run is not worth reading: wrong variant, no readable log,
#   or a fatal AddressSanitizer error.
sanitize_x_trust() {
  local out=$1 variant log
  variant=$(sed -n 's/^[^:]*: variant=\([^ ]*\) .*/\1/p' "$out" | head -n 1)
  if [ "$variant" != "release-sanitize" ]; then
    echo "check-sanitize-x: the harness reported variant='${variant:-none}', not release-sanitize." >&2
    echo "check-sanitize-x: an uninstrumented WM writes a log with no report in it, which reads as a clean run." >&2
    echo "check-sanitize-x: the log is only worth judging when the sanitizer wrote it." >&2
    return 1
  fi
  log=$(sed -n 's/^[^:]*: .*log=//p' "$out" | head -n 1)
  if [ -z "$log" ] || [ ! -f "$log" ]; then
    echo "check-sanitize-x: the harness named no readable log ('${log:-}'), so the run cannot be checked" >&2
    return 1
  fi
  # A fatal ASan error is never accepted: an entry in the baseline says "this
  # allocation site is known", not "reading past a buffer is fine".
  if grep -q 'ERROR: AddressSanitizer:' "$log"; then
    echo "check-sanitize-x: the WM log carries an AddressSanitizer error:" >&2
    grep -A12 -m1 'ERROR: AddressSanitizer:' "$log" >&2
    echo "check-sanitize-x: see $log" >&2
    return 1
  fi
  printf '%s\n' "$log"
}

# sanitize_x_sites <log>
#   One site per leak or UBSan report, on stdout. A leak site is the first stack
#   frame that is a file in this repository, keyed by file and function: the line
#   number moves with any edit above it, and inlining changes which frame ASan
#   reports between two builds of the same source.
sanitize_x_sites() {
  local log=$1
  {
    awk -v root="$ROOT_DIR/" '
      /^(Direct|Indirect) leak of/ { pending = 1; next }
      pending && /^[ \t]*#[0-9]+/ {
        fn = ""; loc = ""
        for (i = 1; i < NF; i++) {
          if ($i == "in") { fn = $(i + 1); loc = $(i + 2); break }
        }
        sub(/:[0-9]+$/, "", loc)
        if (fn != "" && loc ~ /\.(c|h)$/ && index(loc, root) == 1) {
          print substr(loc, length(root) + 1) "|" fn
          pending = 0
        }
        next
      }
      { pending = 0 }
    ' "$log"
    # UBSan does not stop the program, so nothing else in the run would notice
    # it: the message is what gets baselined, not the file.
    sed "s|^$ROOT_DIR/||" "$log" |
      sed -nE 's#^([^ :]+\.(c|h)):[0-9]+:[0-9]+: runtime error: (.*)$#\1|runtime error: \3#p'
  } | sort -u
}

# sanitize_x_verify <harness-output> <baseline>
#   Returns 0 when the run is acceptable and 1 when it is not, printing why.
sanitize_x_verify() {
  local out=$1 baseline=$2 log line site
  log=$(sanitize_x_trust "$out") || return 1

  local -a sites=()
  mapfile -t sites < <(sanitize_x_sites "$log")

  local -A known=() seen=()
  local known_count=0
  if [ -f "$baseline" ]; then
    while IFS= read -r line; do
      case "$line" in '' | '#'*) continue ;; esac
      known["$line"]=1
      known_count=$((known_count + 1))
    done <"$baseline"
  fi

  if [ "${#sites[@]}" -eq 0 ]; then
    if [ "$known_count" -eq 0 ]; then
      echo "check-sanitize-x: no sanitizer report at all"
      return 0
    fi
    echo "check-sanitize-x: the log carries no sanitizer report, but $baseline lists $known_count site(s)."
    echo "check-sanitize-x: either those sites are fixed and the baseline was not updated with them, or the sanitizer did not run."
    echo "check-sanitize-x: the log cannot tell the two apart, so this fails rather than guess."
    echo "check-sanitize-x: if the reports are really gone, run --update-baseline and commit the new baseline with the fix."
    return 1
  fi

  local new=0 stale=0
  for site in "${sites[@]}"; do
    seen["$site"]=1
    if [ -n "${known[$site]:-}" ]; then
      [ "${SANITIZE_X_VERBOSE:-0}" -eq 1 ] && printf '  known  %s\n' "$site"
      continue
    fi
    new=$((new + 1))
    printf '  NEW REPORT  %s\n' "$site"
  done

  for site in "${!known[@]}"; do
    [ -z "${seen[$site]:-}" ] && stale=$((stale + 1)) && printf '  stale baseline entry: %s\n' "$site"
  done

  echo "check-sanitize-x: ${#sites[@]} site(s), $new new"
  [ "$stale" -gt 0 ] && echo "check-sanitize-x: $stale stale baseline entrie(s); run --update-baseline to drop them"

  if [ "$new" -gt 0 ]; then
    echo "A new sanitizer report from a live WM: fix it, or run --update-baseline and say why it is deferred."
    echo "The WM log is $log"
    return 1
  fi
  return 0
}
