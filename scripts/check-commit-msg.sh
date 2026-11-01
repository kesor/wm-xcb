#!/usr/bin/env sh
#
# check-commit-msg.sh - reject AI attribution trailers in a commit message.
#
# Usage: scripts/check-commit-msg.sh <path-to-COMMIT_EDITMSG>
#
# The project rule (AGENTS.md): the git author is the only attribution a commit
# gets. No Co-Authored-By trailers, no "generated with" footers, no bot
# signatures. This is the mechanical half of that rule.
#
# Exit status: 0 when the message is clean, 1 when it carries a trailer.

set -u

msg_file="${1:-}"
if [ -z "$msg_file" ] || [ ! -f "$msg_file" ]; then
  echo "check-commit-msg: no commit message file given" >&2
  exit 1
fi

status=0

# Any Co-Authored-By line at all: the author field is the attribution.
if grep -inE '^[[:space:]]*co-authored-by:' "$msg_file" >/dev/null 2>&1; then
  echo "check-commit-msg: Co-Authored-By trailer is not allowed:" >&2
  grep -inE '^[[:space:]]*co-authored-by:' "$msg_file" >&2
  status=1
fi

# "Generated with", "Made with Claude", and the robot footer.
if grep -inE 'generated (with|by)|made with .*(claude|gpt|copilot|cursor|aider)|🤖' "$msg_file" >/dev/null 2>&1; then
  echo "check-commit-msg: AI attribution footer is not allowed:" >&2
  grep -inE 'generated (with|by)|made with .*(claude|gpt|copilot|cursor|aider)|🤖' "$msg_file" >&2
  status=1
fi

# An empty subject is usually a mistake, not a violation, but it is cheap to
# catch here and it makes the history readable.
subject=$(grep -vE '^[[:space:]]*#' "$msg_file" | sed '/^[[:space:]]*$/d' | head -1)
if [ -z "$subject" ]; then
  echo "check-commit-msg: empty commit message" >&2
  status=1
fi

exit "$status"
