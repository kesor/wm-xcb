#!/usr/bin/env bash
#
# check-flake.sh - evaluate the dev shell, so a broken flake.nix fails the push.
#
# Usage: scripts/check-flake.sh
#
# flake.nix is the entry point for everything here: the shell make and the gates
# run in, direnv, and every CI job. No other gate reads the file, because they
# all run the compiler the shell already provided. CI evaluates it on a push and
# on a pull request, and master in this repository is local-only, so a typo in
# the dev shell used to land before anything had evaluated it and the next
# session opened with "the tree does not build".
#
# The check evaluates the shell and does not build or enter it: a syntax error,
# a missing attribute or a package that no longer exists fails in about a second
# on a warm store, and whether the shell is usable is what the gates that run
# inside it already answer. `nix flake check` would build every output.
#
# Exit status: 0 when the dev shell for this system evaluates, 1 otherwise. No
# nix on PATH fails rather than skipping: the dev shell is not optional here.
#
# The flake attribute is named separately so a rename in flake.nix fails with
# nix's own message, which says which attribute is missing, and not with a shell
# error about an empty variable.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

case "${1:-}" in
  -h | --help)
    awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit 0
    ;;
  "") ;;
  *)
    echo "check-flake: unknown argument '$1'" >&2
    exit 1
    ;;
esac

if ! command -v nix >/dev/null 2>&1; then
  echo "check-flake: nix is not on PATH, so flake.nix cannot be evaluated." >&2
  echo "check-flake: install nix, or run this from the shell that has it." >&2
  exit 1
fi

if [ ! -f flake.nix ]; then
  echo "check-flake: no flake.nix in $ROOT_DIR." >&2
  exit 1
fi

if ! system="$(nix eval --raw --impure --expr 'builtins.currentSystem' 2>&1)"; then
  echo "check-flake: nix cannot report the current system:" >&2
  printf '%s\n' "$system" >&2
  exit 1
fi

attr=".#devShells.$system.default.drvPath"
if ! drv="$(nix eval --raw "$attr" 2>&1)"; then
  echo "check-flake: $attr does not evaluate:" >&2
  printf '%s\n' "$drv" >&2
  echo "check-flake: a broken dev shell stops make, every gate and every agent in this tree." >&2
  exit 1
fi

echo "check-flake: $attr evaluates to $drv"
