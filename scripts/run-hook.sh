#!/usr/bin/env sh
#
# run-hook.sh <hook-name> [args...] - run the lefthook gates for one git hook.
#
# Called by the committed hooks in .githooks/. Kept in one place so the five
# hook files stay three lines each.
#
# When lefthook is already on PATH (inside the dev shell) it runs directly. When
# it is not, the gates run through 'nix develop', so there is still one gate
# list, in lefthook.yml. A machine with neither warns once and exits 0: a
# missing dev shell must not make committing impossible, and CI runs everything.
#
# Deliberately a plain sh script and not a nix shebang: see docs/QUALITY.md.
#
# Exit status: whatever lefthook returns, so a failing gate blocks the commit.

set -u

hook=${1:?usage: run-hook.sh <hook-name> [args...]}
shift

if [ "${WM_HOOKS:-1}" = "0" ]; then
  echo "wm: WM_HOOKS=0, skipping the ${hook} gates"
  exit 0
fi

# --no-auto-install: hooks are committed in .githooks/ and core.hooksPath
# points there, so lefthook has nothing to install and its reminder to run
# 'lefthook install' is wrong for this repository.
if command -v lefthook >/dev/null 2>&1; then
  exec lefthook run "$hook" --no-auto-install "$@"
fi

root=$(git rev-parse --show-toplevel)

if command -v nix >/dev/null 2>&1 && [ -f "$root/flake.nix" ]; then
  echo "wm: lefthook is not on PATH, running the ${hook} gates through nix develop" >&2
  exec nix develop "$root" --command lefthook run "$hook" --no-auto-install "$@"
fi

# The message rule is one script, so it survives a machine without the dev shell.
if [ "$hook" = "commit-msg" ] && [ -x "$root/scripts/check-commit-msg.sh" ]; then
  exec "$root/scripts/check-commit-msg.sh" "$@"
fi

echo "wm: neither lefthook nor nix found, skipping the ${hook} gates." >&2
echo "wm: CI runs them regardless." >&2
exit 0
