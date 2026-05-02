#!/usr/bin/env bash
# review-agent.sh - Review PR changes against documented architecture
#
# Usage:
#   ./scripts/review-agent.sh <PR_number>        # Review PR, leave comments
#   ./scripts/review-agent.sh <PR_number> --fix # Review and attempt auto-fix
#
# This script:
# 1. Gets the diff between PR branch and master
# 2. Reads architecture documentation
# 3. Spawns an agent to review changes
# 4. Posts architecture violation comments on the PR
#
# The agent checks for:
# - Target ownership violations (components referencing targets they don't own)
# - State machine pattern violations (direct state manipulation vs SM transitions)
# - Component coupling (components calling each other directly vs hub)
# - Pipeline misuse (actions not following data/args contract)
# - Missing documentation (new components without architecture docs)
# - ADR conflicts (changes contradicting recorded decisions)

set -e

PR_NUM=""
MODE="review"  # "review" or "fix"
GITHUB_REPO="kesor/wm-xcb"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SUCKLESS_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

usage() {
    echo "Usage: $0 <PR_number> [--fix]"
    echo ""
    echo "Review code changes in a PR against documented architecture."
    echo "Posts comments on the PR explaining any violations."
    echo ""
    echo "Modes:"
    echo "  (default)   Review changes, leave comments on PR"
    echo "  --fix        Attempt to auto-fix violations"
    echo ""
    echo "Examples:"
    echo "  $0 25        # Review PR #25, post comments"
    echo "  $0 25 --fix  # Review and attempt fixes"
    exit 1
}

# Parse arguments
if [ -z "$1" ]; then
    usage
fi

PR_NUM="$1"
shift

if [ "$1" == "--fix" ]; then
    MODE="fix"
    shift
fi

# Get PR info
get_pr_info() {
    gh api repos/${GITHUB_REPO}/pulls/${PR_NUM} --jq '{
        title: .title,
        branch: .head.ref,
        base: .base.ref,
        body: .body
    }' 2>/dev/null
}

# Get diff between PR branch and base branch
get_diff() {
    local branch="$1"
    local base="$2"
    
    # Fetch the base branch and PR branch to compare
    git fetch origin "${base}:refs/remotes/origin/${base}" 2>/dev/null || true
    git fetch origin "${branch}" 2>/dev/null || true
    git diff "origin/${base}...origin/${branch}" -- '*.c' '*.h' 2>/dev/null
}

# Get list of changed files
get_changed_files() {
    local branch="$1"
    local base="$2"
    
    git fetch origin "${base}" 2>/dev/null || true
    git fetch origin "${branch}" 2>/dev/null || true
    git diff "origin/${base}...origin/${branch}" --name-only 2>/dev/null | grep -E '\.(c|h)$' || true
}

# Create review task file
create_review_task() {
    local pr_num="$1"
    local title="$2"
    local branch="$3"
    local diff_size="$4"
    local changed_files="$5"
    local mode="$6"
    
    cat > "${REPO_DIR}/review-task-${pr_num}.txt" << REVEOF
# Task: Architectural Review of PR #${pr_num}

You are reviewing PR #${pr_num}: ${title}

**Mode:** ${mode}

## Context

Read these files to understand the architecture:
- docs/architecture/overview.md — core concepts
- docs/architecture/pipeline-composition.md — action wiring and data flow
- docs/architecture/target.md — target ownership model
- docs/architecture/component.md — component design rules
- docs/architecture/decisions.md — recorded decisions (don't conflict with these)
- docs/architecture/state-machine.md — SM framework rules
- docs/architecture/hub.md — hub communication rules

## PR Information

- Branch: ${branch}
- Changed files: ${changed_files}
- Diff size: ${diff_size} bytes

## Your Review Task

### 1. Read the changes

Run: \`git fetch origin && git diff origin/${BASE}...origin/${BRANCH} -- '*.c' '*.h'\`

Analyze each changed file for architectural violations.

### 2. Check for these violations

**Target Ownership:**
- Is the code referencing targets it doesn't own?
- Components should only access targets they've adopted or that are explicitly shared
- Check: Does focus.c access Client or does it only access "focused-client" target?

**State Machine Pattern:**
- Is state being manipulated directly instead of through SM transitions?
- Check: \`sm_transition(sm, NEW_STATE)\` vs direct assignment

**Component Coupling:**
- Is a component directly calling another component's functions?
- All inter-component communication should go through:
  - hub_send_request() for requests
  - hub_emit()/event subscription for events
- Check: grep for function calls between component files

**Pipeline/Action Contract:**
- Are actions following the data/args contract?
- data = pipeline target (flows through)
- args = per-call arguments

**Documentation Coverage:**
- Are new components documented?
- Check: docs/architecture/ or docs/extensions/

### 3. Generate review comments

For each violation, write a comment in this format:

\`\`\`
## Architecture Violation: [brief title]

**File:** src/components/example.c
**Rule violated:** [from which doc]
**What the code does:** [brief description]
**Why it violates the architecture:** [explanation]
**Suggested fix:** [concrete suggestion]
\`\`\`

### 4. Post comments to PR (${mode} mode)

Use the GitHub CLI to post comments:

\`\`\`bash
# Get authentication for posting
# The comments should be clear and actionable

# Example comment structure:
gh pr comment ${pr_num} --body '...'
\`\`\`

### 5. If --fix mode

For auto-fixable violations:
1. Make the fix in the code
2. Commit with message: "fix(arch): [violation description]"
3. Push: git push

For non-auto-fixable violations:
1. Leave a detailed comment explaining what's wrong
2. Leave a GitHub PR comment for human to address

## Output Format

Produce a JSON summary at the end:

\`\`\`json
{
  "pr": ${pr_num},
  "violations_found": N,
  "blocking": [list of blocking violations],
  "warnings": [list of advisory violations],
  "auto_fixable": [list that can be auto-fixed]
}
\`\`\`

## Example Violation Comment

When a violation is found, post a GitHub PR comment with this format:

\`\`\`markdown
## Architecture Violation: [violation type]

**File:** src/components/example.c:42
**Rule violated:** docs/architecture/[doc].md — "[brief rule summary]"
**What the code does:** [brief description]
**Why it violates:** [explanation of the architectural problem]
**Suggested fix:** [concrete code change or approach]
\`\`\`

## Rules

- Be thorough but constructive
- Reference the specific doc that defines the rule
- Provide concrete suggestions, not just criticism
- If something is ambiguous, flag it as "needing human review"
- Do NOT block the PR for style issues (use clang-format instead)

REVEOF
    echo "Created review task file: review-task-${pr_num}.txt"
}

# Main
cd "$REPO_DIR"

echo "Fetching PR #${PR_NUM} information..."
PR_INFO=$(get_pr_info)
if [ -z "$PR_INFO" ]; then
    echo "Error: Could not fetch PR #${PR_NUM}"
    exit 1
fi

TITLE=$(echo "$PR_INFO" | jq -r '.title')
BRANCH=$(echo "$PR_INFO" | jq -r '.branch')
BASE=$(echo "$PR_INFO" | jq -r '.base')

echo "PR #${PR_NUM}: ${TITLE}"
echo "Branch: ${BRANCH} → ${BASE}"

# Fetch branches
echo "Fetching branches..."
git fetch origin "${BRANCH}" 2>/dev/null || true
git fetch origin "${BASE}" 2>/dev/null || true

# Get changed files
CHANGED_FILES=$(get_changed_files "$BRANCH" "$BASE")
if [ -z "$CHANGED_FILES" ]; then
    echo "No C/H files changed in this PR."
    echo "Nothing to review."
    exit 0
fi

echo "Changed files:"
echo "$CHANGED_FILES" | head -20

# Get diff size
DIFF_SIZE=$(get_diff "$BRANCH" "$BASE" | wc -c)
echo "Diff size: ${DIFF_SIZE} bytes"

# Create review task
echo "Creating review task..."
create_review_task "$PR_NUM" "$TITLE" "$BRANCH" "$DIFF_SIZE" "$CHANGED_FILES" "$MODE"

# Create tmux session for review
WORKTREE_NAME="wm-review-${PR_NUM}"
SESSION_NAME="wm-review"

# Create worktree if needed (for the review)
if [ ! -d "../${WORKTREE_NAME}" ]; then
    echo "Creating worktree for review..."
    git worktree add "../${WORKTREE_NAME}" "origin/${BRANCH}" 2>/dev/null || {
        echo "Worktree exists or failed, continuing with current repo"
        WORKTREE_NAME="suckless/wm"
    }
else
    echo "Using existing worktree for review..."
fi

# Copy architecture docs to review context
if [ -d "../${WORKTREE_NAME}" ]; then
    echo "Copying architecture docs..."
    rm -rf "../${WORKTREE_NAME}/docs"
    cp -r "docs" "../${WORKTREE_NAME}/"
fi

# Copy task file
cp "${REPO_DIR}/review-task-${PR_NUM}.txt" "../${WORKTREE_NAME}/review-task-${PR_NUM}.txt" 2>/dev/null || true

# Create worktree path
if [ "${WORKTREE_NAME}" == "suckless/wm" ]; then
    # Fallback: use current repo
    WORKTREE_PATH="${REPO_DIR}"
    WORKTREE_NAME="wm"
else
    WORKTREE_PATH="${SUCKLESS_ROOT}/${WORKTREE_NAME}"
fi

# Send commands to run the review
NIX_CMD="cd ${SUCKLESS_ROOT} && nix develop --command sh -c 'cd ${WORKTREE_NAME} && pi @review-task-${PR_NUM}.txt Review PR #${PR_NUM} architectural violations'"

# Create or reuse review tmux session
SESSION_NAME="wm-issues"
WINDOW_NAME="review-${PR_NUM}"

# Ensure session exists with correct window
if ! tmux has-session -t ${SESSION_NAME} 2>/dev/null; then
    tmux new-session -d -s ${SESSION_NAME} -n "main" -c "${WORKTREE_PATH}"
fi

# Kill old review window if exists and create new one
tmux kill-window -t "${SESSION_NAME}:${WINDOW_NAME}" 2>/dev/null || true
tmux new-window -t ${SESSION_NAME} -n "${WINDOW_NAME}" -c "${WORKTREE_PATH}"

tmux send-keys -t "${SESSION_NAME}:${WINDOW_NAME}" "git fetch origin" Enter
tmux send-keys -t "${SESSION_NAME}:${WINDOW_NAME}" "${NIX_CMD}" Enter

echo ""
echo "Architectural review agent spawned for PR #${PR_NUM}"
echo "Worktree: ${WORKTREE_PATH}"
echo "Session: ${SESSION_NAME}"
echo "Window: ${WINDOW_NAME}"
echo ""
echo "To attach and monitor: tmux attach -t ${SESSION_NAME}"
echo "To see results: gh pr view ${PR_NUM} --comments"
echo ""
echo "The agent will post comments on the PR explaining any violations found."