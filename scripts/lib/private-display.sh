# shellcheck shell=bash
#
# lib/private-display.sh - give a harness a parent display of its own.
#
# Sourced by scripts/check-seam.sh, scripts/xephyr-test.sh,
# scripts/check-sanitize-x.sh, tests/x/run.sh and tests/x/probe.sh; not
# executable on its own.
#
# Each of those draws a nested Xephyr into a parent display. They used to take
# the session's DISPLAY whenever it was set, and start xvfb-run only when it was
# not, so with DISPLAY=:0 in the dev shell every seam run and every pre-push gate
# put Xephyr windows on the desktop of whoever was working and took the input
# focus with -noreset. The answer also depended on that display's depth and
# health, and CI, which has no display at all, verified a different code path
# than the one a developer's machine took.
#
# The parent display is now a private Xvfb owned by the run, whatever DISPLAY
# says, and the caller is re-executed inside it. WM_USE_SESSION_DISPLAY=1 is the
# way back to the session display for someone watching the harness by eye; it is
# documented in docs/TESTING.md.

# wm_parent_display <the script's own arguments...>
#   Ensures the calling script has a parent display to nest Xephyr into, prints
#   one line naming it, and exports
#     WM_PARENT_DISPLAY       the display to nest into
#     WM_PARENT_DISPLAY_KIND  "private" or "session"
#     WM_SESSION_DISPLAY      the DISPLAY the run was started with, or empty
#   The line is left in WM_PARENT_DISPLAY_LINE so a caller can record it without
#   capturing stdout, the way scripts/lib/wm-variant.sh leaves WM_VARIANT_LINE.
#   Nest into WM_PARENT_DISPLAY, not into DISPLAY: a caller that has already
#   started a nested server has pointed DISPLAY at that server.
#
#   When the process has no private display yet and WM_USE_SESSION_DISPLAY=1 is
#   not set, this does not return: it re-executes the script under xvfb-run with
#   the arguments passed in, so a caller has to pass its own "$@" unchanged, not
#   what is left of them after its option parsing.
#
#   Returns non-zero when no parent display can be had: xvfb-run is missing, or
#   WM_USE_SESSION_DISPLAY=1 was asked for and DISPLAY is unset. It never falls
#   back to the session display on its own, because that fallback is the defect
#   this replaces.
#
#   xvfb-run kills the private server when the script exits, so --keep cannot
#   outlive a hermetic run: a session kept for inspection is the one case that
#   wants WM_USE_SESSION_DISPLAY=1 as well.
wm_parent_display() {
  local script session parent
  script=$(basename "$0")
  session=${DISPLAY:-}

  if [ "${WM_USE_SESSION_DISPLAY:-0}" = "1" ]; then
    if [ -z "$session" ]; then
      echo "$script: WM_USE_SESSION_DISPLAY=1 but DISPLAY is unset, so there is no session display to draw into." >&2
      echo "$script: unset WM_USE_SESSION_DISPLAY to run in a private Xvfb." >&2
      return 1
    fi
    WM_PARENT_DISPLAY=$session
    WM_PARENT_DISPLAY_KIND=session
    WM_SESSION_DISPLAY=$session
    WM_PARENT_DISPLAY_LINE="$script: parent display = $WM_PARENT_DISPLAY (session display, WM_USE_SESSION_DISPLAY=1)"
    export WM_PARENT_DISPLAY WM_PARENT_DISPLAY_KIND WM_SESSION_DISPLAY
    echo "$WM_PARENT_DISPLAY_LINE"
    return 0
  fi

  # A harness that another harness started (the seam suite runs
  # scripts/xephyr-test.sh) nests into the display the outer run owns. Starting
  # a second Xvfb there would leave a server per level for no gain, and the
  # outer one is killed when the outermost script exits.
  #
  # Prefer WM_PARENT_DISPLAY when it is already set. run.sh points DISPLAY at
  # the nested server before it runs tests, so the current DISPLAY is the
  # server under test, not the private parent. The re-executed script itself
  # has no WM_PARENT_DISPLAY yet: xvfb-run has set DISPLAY, and that is the
  # parent.
  if [ "${WM_PRIVATE_PARENT_DISPLAY:-0}" = "1" ]; then
    parent=${WM_PARENT_DISPLAY:-$session}
    if [ -n "$parent" ]; then
      WM_PARENT_DISPLAY=$parent
      WM_PARENT_DISPLAY_KIND=private
      WM_PARENT_DISPLAY_LINE="$script: parent display = $WM_PARENT_DISPLAY (private Xvfb), session display = ${WM_SESSION_DISPLAY:-none}"
      export WM_PARENT_DISPLAY WM_PARENT_DISPLAY_KIND
      echo "$WM_PARENT_DISPLAY_LINE"
      return 0
    fi
  fi

  if ! command -v xvfb-run >/dev/null 2>&1 || ! command -v Xvfb >/dev/null 2>&1; then
    echo "$script: xvfb-run and Xvfb are not both on PATH, so no private display can start." >&2
    echo "$script: start the dev shell ('direnv allow' or 'nix develop') and run this again." >&2
    echo "$script: WM_USE_SESSION_DISPLAY=1 draws into the session's DISPLAY instead, which is not hermetic." >&2
    return 1
  fi

  WM_SESSION_DISPLAY=$session
  export WM_SESSION_DISPLAY
  echo "$script: starting a private Xvfb for the parent display (WM_USE_SESSION_DISPLAY=1 uses the session's instead)"
  # -a picks a display number nothing else is using, and the screen is the
  # geometry the nested server gets anyway, so the parent's size cannot change
  # what a test measures.
  # Drop a caller-exported WM_PARENT_DISPLAY. The re-executed script prefers
  # that variable over DISPLAY, and a stale value would name the session
  # instead of the server xvfb-run is about to start.
  unset WM_PARENT_DISPLAY WM_PARENT_DISPLAY_KIND
  exec xvfb-run -a -s "-screen 0 ${WM_PARENT_SCREEN:-1280x800x24}" \
    env WM_PRIVATE_PARENT_DISPLAY=1 "${BASH:-bash}" "$0" "$@"
}

# wm_display_free <n>
#   True when display :<n> can be used: nothing is listening on its socket and
#   no live lock is held. A lock file with no socket next to it is a server that
#   died without cleaning up - a crashed Xvfb, a machine that lost power - and it
#   used to take that display out of the pool for every later run: one eighth of
#   the pool, silently, until someone deleted the file by hand.
#
#   The lock is removed only when the socket is absent and the lock is older than
#   WM_STALE_LOCK_SECONDS (default 30), which is longer than the gap between an X
#   server creating its lock and creating its socket. The removal is reported, so
#   a person reading a run's log knows a stale lock was cleared rather than
#   wondering why a display number changed.
wm_display_free() {
  local n=$1 socket="/tmp/.X11-unix/X$1" lock="/tmp/.X$1-lock" age now mtime
  [ -e "$socket" ] && return 1
  if [ ! -e "$lock" ]; then
    return 0
  fi
  age=${WM_STALE_LOCK_SECONDS:-30}
  now=$(date +%s)
  mtime=$(date -r "$lock" +%s 2>/dev/null || echo "$now")
  if [ $((now - mtime)) -lt "$age" ]; then
    # A server may be between creating its lock and its socket.
    return 1
  fi
  if rm -f "$lock" 2>/dev/null; then
    echo "display :$n had a $((now - mtime))s-old lock and no socket; cleared the stale lock" >&2
    return 0
  fi
  return 1
}
