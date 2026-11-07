# shellcheck shell=bash
#
# lib/root-report.sh - hold the repository root to an allowlist of entries.
#
# Sourced by scripts/check-root.sh, which reads the tracked root entries from
# git, and by tests/x/test-115-root-report.sh, which feeds this recorded lists
# so every shape is pinned without a dirty tree.
#
# The root is an index: the build files, the documentation entry points and the
# directories that group the code. A source file there belongs to no group, so a
# reader has to open the Makefile to learn what it is, and every new file has to
# guess where it goes. The lists below are that shape; a new kind of root entry
# is a deliberate edit here, made by the commit that adds it.

ROOT_ALLOWED_FILES=(
  AGENTS.md
  README.md
  Makefile
  Dockerfile
  flake.nix
  flake.lock
  lefthook.yml
  .clang-format
  .clang-tidy
  .dockerignore
  .editorconfig
  .envrc
  .gitignore
)

ROOT_ALLOWED_DIRS=(
  .agents
  .beads
  .githooks
  .github
  docs
  plans
  scripts
  src
  tests
  tools
  vendor
)

# root_verify <entries-file> <gate>
#   0 when every top-level name in the file is allowed,
#   1 when one is not, or when the file is empty: an empty list is a read that
#   failed, not a root with nothing in it.
root_verify() {
  local entries=$1 gate=$2
  local -A allowed=()
  local name
  local checked=0 bad=0

  if [ ! -s "$entries" ]; then
    echo "$gate: the root entry list is empty, so git ls-files gave nothing to check"
    return 1
  fi

  for name in "${ROOT_ALLOWED_FILES[@]}" "${ROOT_ALLOWED_DIRS[@]}"; do
    allowed["$name"]=1
  done

  while IFS= read -r name; do
    [ -n "$name" ] || continue
    checked=$((checked + 1))
    if [ -n "${allowed[$name]:-}" ]; then
      continue
    fi
    bad=$((bad + 1))
    printf '  NOT ALLOWED  %s\n' "$name"
    case "$name" in
      *.c | *.h)
        printf '               a source file belongs in a directory under src/\n'
        ;;
    esac
  done <"$entries"

  if [ "$bad" -eq 0 ]; then
    [ "${ROOT_VERBOSE:-0}" -eq 1 ] &&
      echo "$gate: $checked root entry(s), every one on the allowlist"
    return 0
  fi

  echo
  echo "$gate: $bad root entry(s) outside the allowlist."
  echo "The root holds the build files, the documentation entry points and the"
  echo "directories that group the code. Put a source file in the directory it"
  echo "belongs to. A new kind of root entry needs its line in"
  echo "scripts/lib/root-report.sh, added by the commit that introduces it."
  return 1
}
