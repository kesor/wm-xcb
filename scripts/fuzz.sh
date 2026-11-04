#!/usr/bin/env bash
#
# fuzz.sh - build the fuzz harnesses with clang and run them for a while.
#
# Usage: scripts/fuzz.sh [seconds-per-target] [target...]
#
# Each target is a libFuzzer binary in build/fuzz/. libFuzzer keeps the inputs
# that reach new code, so a crash comes with the bytes that produced it, and the
# corpus in tests/fuzz/corpus/<target>/ is what later runs start from.
#
# Why clang and not the gcc the build uses: -fsanitize=fuzzer is clang's, and the
# coverage feedback that makes fuzzing more than random input is clang's too.
#
# Exit status: 0 when every target ran without a crash, 1 otherwise. A crash
# writes its reproducer to build/fuzz/<target>-crash-<sha1>.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SECONDS_PER_TARGET=${1:-20}
shift || true

if ! command -v clang >/dev/null 2>&1; then
  echo "fuzz: clang is not on PATH. Start the dev shell: 'direnv allow' or 'nix develop'." >&2
  exit 1
fi

OUT=build/fuzz
mkdir -p "$OUT"

# The WM objects the harnesses link against. wm.c is the event loop and not
# needed; the rest is what a dispatch and a rule lookup reach.
SOURCES=(
  config.c wm-log.c wm-hub.c wm-states.c wm-components.c wm-running.c wm-signals.c
  wm-xcb.c wm-xcb-ewmh.c wm-xcb-events.c
  src/sm/sm-instance.c src/sm/sm-registry.c src/sm/sm-template.c
  src/xcb/xcb-handler.c
  src/target/client.c src/target/monitor.c src/target/tag.c
  src/components/client-list.c src/components/connection-sm.c src/components/focus.c
  src/components/fullscreen.c src/components/keybinding.c src/components/monitor-manager.c
  src/components/pertag.c src/components/tag-manager.c src/components/tiling.c
  src/actions/action-registry.c src/actions/keybinding-binding.c src/actions/launcher.c
  src/actions/terminal.c
)

read -r -a PKG_CFLAGS <<<"$(pkg-config --cflags xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms)"
read -r -a PKG_LIBS <<<"$(pkg-config --libs xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms)"

SANITIZE=(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
CFLAGS=(
  -g -O1 -std=gnu11 -DWM_HUB_TESTING -D_DEFAULT_SOURCE -DVERSION=\"0.0.1\"
  -fsanitize=fuzzer-no-link "${SANITIZE[@]}"
  "${PKG_CFLAGS[@]}"
  -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include
)
LDFLAGS=(
  -fsanitize=fuzzer "${SANITIZE[@]}"
  "${PKG_LIBS[@]}" -pthread
)

objects=()
for src in "${SOURCES[@]}"; do
  obj="$OUT/${src//\//_}.o"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    if ! clang "${CFLAGS[@]}" -w -c "$src" -o "$obj"; then
      echo "fuzz: cannot compile $src" >&2
      exit 1
    fi
  fi
  objects+=("$obj")
done

status=0
for harness in tests/fuzz/fuzz-*.c; do
  name=$(basename "$harness" .c)
  if [ "$#" -gt 0 ]; then
    wanted=0
    for want in "$@"; do [ "$want" = "$name" ] && wanted=1; done
    [ "$wanted" = 1 ] || continue
  fi
  bin="$OUT/$name"
  if ! clang "${CFLAGS[@]}" -w "$harness" "${objects[@]}" "${LDFLAGS[@]}" -o "$bin"; then
    echo "fuzz: cannot link $name" >&2
    exit 1
  fi
  corpus="tests/fuzz/corpus/${name#fuzz-}"
  mkdir -p "$corpus"
  echo "--- $name for ${SECONDS_PER_TARGET}s (corpus $corpus)"
  if ! "$bin" -max_total_time="$SECONDS_PER_TARGET" -print_final_stats=1 \
      -artifact_prefix="$OUT/$name-crash-" "$corpus"; then
    echo "fuzz: $name found a crash; the input is in $OUT" >&2
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo "fuzz: every target ran for ${SECONDS_PER_TARGET}s without a crash"
fi
exit "$status"
