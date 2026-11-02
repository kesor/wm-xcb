# shellcheck shell=bash
#
# lib/wm-variant.sh - build the WM variant a harness asked for, and say which.
#
# Sourced by scripts/xephyr-test.sh and tests/x/run.sh; not executable on its
# own. The caller sets ROOT_DIR, may export DEBUG and SANITIZE with the same
# meaning as the make targets, and gets back WM_VARIANT_BIN.
#
# The Makefile has a default variant, and a harness that runs plain `make` gets
# that default whatever it was asked for. A DEBUG=1 harness therefore built
# release, wrote no DEBUG lines, and the empty log read as "the event never
# happened". The variant is passed as a command-line assignment, which beats any
# default in the file, and the line that names it is printed here so a harness
# cannot build one variant and report another.

# wm_build_variant <log-path>
#   Builds build/<variant>/wm for the DEBUG and SANITIZE in the environment,
#   prints
#     <script>: variant=<variant> (DEBUG=<d> SANITIZE=<s>), binary=<path>, log=<path>
#   and leaves the path in WM_VARIANT_BIN, the stdbuf choice in
#   WM_VARIANT_STDBUF, and the line itself in WM_VARIANT_LINE so a caller that
#   needs to inspect it does not have to capture stdout in a subshell, which
#   would throw the paths away.
wm_build_variant() {
  local log=$1 debug="${DEBUG:-0}" sanitize="${SANITIZE:-0}" variant
  local script
  script=$(basename "$0")

  variant=$(make -s -C "$ROOT_DIR" print-variant DEBUG="$debug" SANITIZE="$sanitize") || {
    echo "$script: cannot read the build variant from make" >&2
    return 1
  }
  if ! make -s -C "$ROOT_DIR" wm DEBUG="$debug" SANITIZE="$sanitize" >/dev/null; then
    echo "$script: the WM does not build with DEBUG=$debug SANITIZE=$sanitize; see make" >&2
    return 1
  fi

  WM_VARIANT_BIN="$ROOT_DIR/build/$variant/wm"
  if [ ! -x "$WM_VARIANT_BIN" ]; then
    echo "$script: make reported variant '$variant' but $WM_VARIANT_BIN is missing" >&2
    return 1
  fi
  # stdbuf is what keeps DEBUG lines in the log when the WM is killed rather
  # than exited, but it works by preloading a library, and ASan refuses to start
  # unless its runtime comes first in that list ("ASan runtime does not come
  # first in initial library list"). A sanitize build has no DEBUG lines to
  # keep, so it runs without stdbuf rather than with a disabled link-order
  # check.
  case "$variant" in
    *-sanitize) WM_VARIANT_STDBUF=0 ;;
    *) WM_VARIANT_STDBUF=1 ;;
  esac
  WM_VARIANT_LINE="$script: variant=$variant (DEBUG=$debug SANITIZE=$sanitize), binary=$WM_VARIANT_BIN, log=$log"
  echo "$WM_VARIANT_LINE"
}
