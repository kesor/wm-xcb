#!/usr/bin/env bash
#
# check-workflow.sh - parse every file under .github/workflows/.
#
# Usage: scripts/check-workflow.sh [file...]
#
# .github/workflows/quality.yml is the CI half of the gates. Nothing in the
# repository read it, and a worker cannot push to watch the run, so a syntax
# error or a moved key landed on master and the next push had no gates at all:
# the `gates` job never started, and nothing said why.
#
# The tool is actionlint, because it is the one that reads the workflow schema:
# plain YAML would parse a misspelled key and an `on:` that is the boolean true,
# and neither is a workflow CI will run. It also lints the shell inside `run:`
# steps, which is where the gates call each other.
#
# A gate inside CI cannot catch this: the check lives in the same workflow it
# checks, so the run that would report a broken file is the run the broken file
# removed. This one runs before the push, in lefthook.yml.
#
# Exit status: 0 when every workflow parses, 1 otherwise. actionlint missing
# fails rather than skipping, like scripts/check-flake.sh: the dev shell carries
# it, and a check that does not run has nothing to say.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

FILES=()
for arg in "$@"; do
  case "$arg" in
    -h | --help)
      awk 'NR > 1 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
      exit 0
      ;;
    *) FILES+=("$arg") ;;
  esac
done

if ! command -v actionlint >/dev/null 2>&1; then
  echo "check-workflow: actionlint is not on PATH, so the workflows cannot be parsed." >&2
  echo "check-workflow: it is in the dev shell: direnv allow, or nix develop -c $0" >&2
  exit 1
fi

if [ "${#FILES[@]}" -eq 0 ]; then
  mapfile -t FILES < <(git ls-files '.github/workflows/*.yml' '.github/workflows/*.yaml')
fi
if [ "${#FILES[@]}" -eq 0 ]; then
  echo "check-workflow: no workflow files to parse"
  exit 0
fi

if actionlint "$@"; then
  echo "check-workflow: ${#FILES[@]} workflow file(s) parse"
  exit 0
fi
echo "check-workflow: a workflow file does not parse. CI runs lefthook, so a" >&2
echo "check-workflow: broken file here means the next push runs no gates at all." >&2
exit 1
