#!/usr/bin/env bash
#
# test-86-queued-event.sh - an event libxcb has already read is still dispatched.
#
# poll(2) reports the X socket, not libxcb's own queue. A reply wait or a flush
# can move an event from the socket into that queue, and then poll(2) waits with
# the event already queued. The loop has to look at the queue before it blocks.
#
# The startup path is the deterministic case. setup_ewmh() waits for the server
# after substructure redirect is selected and before the adoption scan. This
# test holds its first atom query with a preloaded shim, maps a window while the
# WM is held, and then releases it. The map is redirected, so the window is not
# viewable and the adoption scan skips it. The reply wait reads that MapRequest
# into libxcb's queue, and a loop that only polls the socket never dispatches
# it.
#
# This starts its own Xvfb and WM: the suite's WM is already past startup, and
# the condition cannot be produced once the loop is running. The client is run
# with -y so the server has processed its map, and sent the MapRequest to the
# WM, before the shim is released.

set -uo pipefail

source "$(dirname "$0")/lib.sh"

SHIM="$ROOT_DIR/tools/xcb-ewmh-delay.so"
if [ ! -f "$SHIM" ]; then
  fail "the reply-delay shim is built (make tools/xcb-ewmh-delay.so)"
  finish
  exit 1
fi
if ! command -v Xvfb >/dev/null 2>&1; then
  fail "Xvfb is available to start a private display"
  finish
  exit 1
fi

DIR="$TMPDIR_SEAM/queued-event"
rm -rf "$DIR"
mkdir -p "$DIR"
MARKER="$DIR/ready"
GO="$DIR/go"
QUEUE_WM_LOG="$DIR/wm.log"
XVFB_LOG="$DIR/xvfb.log"
CLIENT_OUT="$DIR/client.out"

DISPLAY_NUM=""
for n in 20 21 22 23 24 25 26 27 28 29; do
  if [ ! -e "/tmp/.X11-unix/X$n" ] && [ ! -e "/tmp/.X$n-lock" ]; then
    DISPLAY_NUM=$n
    break
  fi
done
if [ -z "$DISPLAY_NUM" ]; then
  fail "a free display in :20..:29"
  finish
  exit 1
fi

VARIANT=$(make -s -C "$ROOT_DIR" print-variant DEBUG="${WM_SEAM_DEBUG:-0}" SANITIZE="${SANITIZE:-0}")
WM_BIN="$ROOT_DIR/build/$VARIANT/wm"
if [ ! -x "$WM_BIN" ]; then
  fail "the WM under test exists ($WM_BIN)"
  finish
  exit 1
fi

QUEUE_DISPLAY=":$DISPLAY_NUM"
CLASS="queued-$$-$RANDOM"
XVFB_PID=""
QWM_PID=""
CLIENT_PID=""

# A sanitized build refuses to start when anything is preloaded before the ASan
# runtime, so put the runtime first in that variant.
PRELOAD="$SHIM"
case "$VARIANT" in
  *sanitize*)
    ASAN_LIB=$(gcc -print-file-name=libasan.so)
    if [ -f "$ASAN_LIB" ]; then
      PRELOAD="$ASAN_LIB:$SHIM"
    fi
    ;;
esac

cleanup() {
  [ -n "$CLIENT_PID" ] && kill "$CLIENT_PID" 2>/dev/null
  [ -n "$QWM_PID" ] && kill "$QWM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

Xvfb "$QUEUE_DISPLAY" -screen 0 800x600x24 -noreset >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

for _ in $(seq 1 100); do
  DISPLAY="$QUEUE_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1 && break
  sleep 0.05
done
if ! DISPLAY="$QUEUE_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
  fail "a private display is up ($QUEUE_DISPLAY)"
  tail -n 10 "$XVFB_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "a private display is up ($QUEUE_DISPLAY)"

# The shim has to be preloaded into the WM, not into the client: it holds
# xcb_ewmh_init_atoms(), which only setup_ewmh() calls.
env DISPLAY="$QUEUE_DISPLAY" \
  LD_PRELOAD="$PRELOAD" \
  WM_QUEUE_TEST_MARKER="$MARKER" \
  WM_QUEUE_TEST_GO="$GO" \
  "$WM_BIN" >"$QUEUE_WM_LOG" 2>&1 &
QWM_PID=$!

held=0
for _ in $(seq 1 100); do
  if [ -f "$MARKER" ]; then
    held=1
    break
  fi
  if ! kill -0 "$QWM_PID" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
if [ "$held" -ne 1 ]; then
  fail "the WM reached the held startup reply"
  tail -n 15 "$QUEUE_WM_LOG" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi
pass "the WM is held in the startup atom query"

DISPLAY="$QUEUE_DISPLAY" "$XCLIENT" -n 1 -y -v -W 300 -H 200 \
  -t queued -c "$CLASS" >"$CLIENT_OUT" 2>&1 &
CLIENT_PID=$!

mapped=0
for _ in $(seq 1 200); do
  if [ -s "$CLIENT_OUT" ]; then
    mapped=1
    break
  fi
  sleep 0.01
done
if [ "$mapped" -ne 1 ]; then
  fail "the client created and mapped a window"
  tail -n 10 "$CLIENT_OUT" 2>/dev/null | sed 's/^/        /'
  finish
  exit 1
fi

# The client's -y round trip has returned, so the server has seen the map and
# sent the MapRequest. Release the WM: the reply wait reads that MapRequest into
# libxcb's queue.
: >"$GO"

visible=0
for _ in $(seq 1 200); do
  if [ -n "$(DISPLAY="$QUEUE_DISPLAY" xdotool search --onlyvisible --class "$CLASS" 2>/dev/null)" ]; then
    visible=1
    break
  fi
  if ! kill -0 "$QWM_PID" 2>/dev/null; then
    break
  fi
  sleep 0.01
done

if [ "$visible" -eq 1 ]; then
  pass "the queued MapRequest was dispatched and the window is viewable"
else
  fail "the queued MapRequest was dispatched and the window is viewable"
  tail -n 15 "$QUEUE_WM_LOG" 2>/dev/null | sed 's/^/        /'
fi

if kill -0 "$QWM_PID" 2>/dev/null; then
  pass "WM still running"
else
  fail "WM died; last log lines:"
  tail -n 15 "$QUEUE_WM_LOG" 2>/dev/null | sed 's/^/        /'
fi

finish
