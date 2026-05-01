#!/usr/bin/env bash
# spawn-agent.sh - Spawn a pi agent to work on a GitHub issue
#
# Usage:
#   ./spawn-agent.sh <issue_number>              # Start new work on issue
#   ./spawn-agent.sh <issue_number> --fix        # Fix PR with comments
#   ./spawn-agent.sh <issue_number> --continue    # Continue existing session
#
# This script:
# 1. Creates a git worktree from the main repo (or uses existing)
# 2. Copies docs and plans to the worktree
# 3. For --fix: fetches PR comments, creates fix task
# 4. Opens a new tmux window
# 5. Runs pi with the task file (new or resumed)

set -e

ISSUE_NUM=""
MODE="new"  # "new", "fix", or "continue"
GITHUB_REPO="kesor/wm-xcb"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
SUCKLESS_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

usage() {
    echo "Usage: $0 <issue_number|PR_number> [--fix|--continue]"
    echo ""
    echo "Modes:"
    echo "  (default)   Start new work on issue"
    echo "  --fix        Fix PR with code review comments (fetches comments, creates task)"
    echo "  --continue   Continue existing session"
    echo ""
    echo "Examples:"
    echo "  $0 2          # Start working on issue #2"
    echo "  $0 25 --fix  # Fix issues in PR #25"
    echo "  $0 25        # Same as --fix for PRs"
    exit 1
}

# Parse arguments
if [ -z "$1" ]; then
    usage
fi

ISSUE_NUM="$1"
shift

# Check for mode flags
if [ "$1" == "--fix" ]; then
    MODE="fix"
    shift
elif [ "$1" == "--continue" ]; then
    MODE="continue"
    shift
elif [ "$1" == "--new" ]; then
    MODE="new"
    shift
fi

WORKTREE_NAME="wm-issue-${ISSUE_NUM}"
TASK_FILE="task-issue-${ISSUE_NUM}.txt"
BRANCH_NAME="issue-${ISSUE_NUM}"

# Function to get issue body (markdown)
get_issue_body() {
    local issue_num="$1"
    gh api repos/${GITHUB_REPO}/issues/${issue_num} --jq '.body' 2>/dev/null
}

# Function to get actual branch name from PR
get_branch_name() {
    local pr_num="$1"
    gh api repos/${GITHUB_REPO}/pulls/${pr_num} --jq '.head.ref' 2>/dev/null
}

# Find existing PR for branch
get_pr_for_branch() {
    local branch="$1"
    gh api repos/${GITHUB_REPO}/pulls --jq ".[] | select(.head.ref == \"${branch}\") | .number" 2>/dev/null | head -1
}

# Function to fetch PR comments
fetch_pr_comments() {
    local pr_num="$1"
    echo "Fetching comments from PR #${pr_num}..."
    gh api repos/${GITHUB_REPO}/pulls/${pr_num}/comments --jq '.[] | "**\(.user.login):**\n\(.body)\n---"' 2>/dev/null || \
    gh api repos/${GITHUB_REPO}/issues/${pr_num}/comments --jq '.[] | "**\(.user.login):**\n\(.body)\n---"' 2>/dev/null || \
    echo "Could not fetch comments"
}

# Function to get issue/PR title
get_title() {
    local num="$1"
    # Try issues first (for issues), then pulls (for PRs)
    gh api repos/${GITHUB_REPO}/issues/${num} --jq '.title' 2>/dev/null || \
    gh api repos/${GITHUB_REPO}/pulls/${num} --jq '.title' 2>/dev/null
}

# Fetch comments and create fix task file
create_fix_task() {
    local pr_num="$1"
    local title="$2"
    local comments=$(fetch_pr_comments "$pr_num")
    local task_file="${MAIN_REPO}/${TASK_FILE}"

    cat > "${task_file}" << FIXEOF
# Task: Fix Code Review Comments (PR #${pr_num})

You are fixing code review comments on PR #${pr_num}: ${title}

## Context

Read the GitHub PR for context:
- https://github.com/${GITHUB_REPO}/pull/${pr_num}

Read these files first for architecture context:
- docs/architecture/overview.md — core concepts
- docs/architecture/hub.md — hub design details

## Code Review Comments

The following comments were left on the PR:

${comments}

## Your Task

1. Read and understand the code review comments
2. Make the necessary fixes to address each comment
3. Commit your changes with a meaningful commit message
4. Push to the existing branch: git push

## Rules

- Follow existing code style (see wm-window-list.c)
- Address ALL comments
- If a comment is unclear, ask for clarification in a PR comment
- Push and the PR will be updated automatically

FIXEOF
    echo "Created fix task file with PR comments"
}

# Main logic
cd "$MAIN_REPO"

# Fetch latest from origin
git fetch origin

# For PRs, get the actual branch name
if [ "$MODE" == "fix" ] || [ "$MODE" == "continue" ]; then
    PR_NUM="${ISSUE_NUM}"
    BRANCH_NAME=$(get_branch_name "$PR_NUM")
    if [ -z "$BRANCH_NAME" ]; then
        echo "Error: Could not find PR #${PR_NUM}"
        exit 1
    fi
    echo "PR #${PR_NUM} uses branch: ${BRANCH_NAME}"
fi

# Check if there's an existing PR for this branch (for --new mode)
if [ "$MODE" == "new" ]; then
    EXISTING_PR=$(get_pr_for_branch "${BRANCH_NAME}")
    if [ -n "$EXISTING_PR" ]; then
        MODE="fix"
        echo "Found existing PR #${EXISTING_PR} for issue #${ISSUE_NUM}"
    fi
fi

# Get session path for resume
if [ "$MODE" == "fix" ] || [ "$MODE" == "continue" ]; then
    PR_NUM="${EXISTING_PR:-$ISSUE_NUM}"
    PR_TITLE=$(get_title "$PR_NUM")

    if [ -z "$PR_TITLE" ]; then
        echo "Error: Could not find PR #${PR_NUM}"
        exit 1
    fi

    echo "Continuing work on PR #${PR_NUM}: ${PR_TITLE}"

    # Find existing worktree for this branch
    WORKTREE_PATH=$(git worktree list --porcelain 2>/dev/null | grep -B2 "branch refs/heads/${BRANCH_NAME}$" | head -1 | awk '{print $2}')
    if [ -n "$WORKTREE_PATH" ] && [ -d "$WORKTREE_PATH" ]; then
        WORKTREE_NAME=$(basename "$WORKTREE_PATH")
        echo "Using existing worktree: $WORKTREE_PATH (${WORKTREE_NAME})"
    elif [ -d "../${WORKTREE_NAME}" ]; then
        echo "Using existing worktree: ../${WORKTREE_NAME}"
    else
        echo "Creating worktree: ../${WORKTREE_NAME}"
        git worktree add "../${WORKTREE_NAME}" -b "${BRANCH_NAME}" origin/master
    fi

    # Create fix task with comments (only for fix mode)
    if [ "$MODE" == "fix" ]; then
        create_fix_task "$PR_NUM" "$PR_TITLE"
    elif [ "$MODE" == "continue" ]; then
        # --continue mode: use existing task file if available
        if [ -f "${MAIN_REPO}/task-issue-${ISSUE_NUM}.txt" ]; then
            cp "${MAIN_REPO}/task-issue-${ISSUE_NUM}.txt" "../${WORKTREE_NAME}/"
        fi
    fi

    # Always copy task file to worktree for pi to find
    cp "${MAIN_REPO}/${TASK_FILE}" "../${WORKTREE_NAME}/"
else
    # New work mode - agent will fetch issue details from GitHub
    ISSUE_TITLE=$(get_title "$ISSUE_NUM")
    if [ -z "$ISSUE_TITLE" ]; then
        echo "Error: Could not fetch issue #${ISSUE_NUM}"
        exit 1
    fi
    echo "Issue #${ISSUE_NUM}: ${ISSUE_TITLE}"
    echo "Agent will fetch issue details from GitHub."

    # Create worktree if needed
    if [ -d "../${WORKTREE_NAME}" ]; then
        echo "Using existing worktree: ../${WORKTREE_NAME}"
    else
        echo "Creating worktree: ../${WORKTREE_NAME}"
        git worktree add "../${WORKTREE_NAME}" -b "${BRANCH_NAME}" origin/master
    fi
fi

# Copy common files to worktree
echo "Copying files to worktree..."
rm -rf "../${WORKTREE_NAME}/docs"
cp -r "docs" "../${WORKTREE_NAME}/"
rm -rf "../${WORKTREE_NAME}/plans"
cp -r "plans" "../${WORKTREE_NAME}/"
rm -rf "../${WORKTREE_NAME}/vendor"
cp -r "vendor" "../${WORKTREE_NAME}/" 2>/dev/null || true

# Ensure task files are gitignored
if ! grep -q "task-.*.txt" "../${WORKTREE_NAME}/.gitignore" 2>/dev/null; then
    echo "task-*.txt" >> "../${WORKTREE_NAME}/.gitignore"
fi

# Create tmux window in the wm-issues session
if [ "$MODE" == "fix" ] || [ "$MODE" == "continue" ]; then
    WINDOW_NAME="pr-${ISSUE_NUM}"
else
    WINDOW_NAME="issue-${ISSUE_NUM}"
fi

# Use or create the wm-issues session
WORKTREE_PATH="${SUCKLESS_ROOT}/${WORKTREE_NAME}"
if tmux has-session -t wm-issues 2>/dev/null; then
    # Kill existing window with same name if it exists, ignore errors
    tmux kill-window -t "wm-issues:$WINDOW_NAME" 2>/dev/null || true
    tmux new-window -t wm-issues -n "$WINDOW_NAME" -c "${WORKTREE_PATH}"
else
    tmux new-session -d -s wm-issues -n "$WINDOW_NAME" -c "${WORKTREE_PATH}"
fi

# Send commands to the new window
tmux send-keys -t "wm-issues:$WINDOW_NAME" "git fetch origin && git pull --rebase origin ${BRANCH_NAME}" Enter

# Build pi command - run inside nix develop environment
# nix develop needs the root (has flake.nix), but worktree is in subdirectory
# Use nix develop from parent, then cd to worktree for pi
NIX_CMD="cd ${SUCKLESS_ROOT} && nix develop --command sh -c 'cd ${WORKTREE_NAME} && pi "

if [ "$MODE" == "fix" ]; then
    # For fixes, use existing task file
    TASK_DESC="Fix code review comments on PR #${ISSUE_NUM}"
    if [ -f "${MAIN_REPO}/${TASK_FILE}" ]; then
        tmux send-keys -t "wm-issues:$WINDOW_NAME" "${NIX_CMD}@task-issue-${ISSUE_NUM}.txt ${TASK_DESC}'" Enter
    else
        tmux send-keys -t "wm-issues:$WINDOW_NAME" "${NIX_CMD}${TASK_DESC}'" Enter
    fi
else
    # For new issues, copy template and fill in issue-specific details
    TASK_FILE="task-issue-${ISSUE_NUM}.txt"
    cp "${MAIN_REPO}/scripts/task-template.md" "${MAIN_REPO}/${TASK_FILE}"
    sed -i "s/{TITLE}/Issue #${ISSUE_NUM}: ${ISSUE_TITLE}/g" "${MAIN_REPO}/${TASK_FILE}"
    sed -i "s/{ISSUE}/${ISSUE_NUM}/g" "${MAIN_REPO}/${TASK_FILE}"
    sed -i "s/{BRANCH_NAME}/${BRANCH_NAME}/g" "${MAIN_REPO}/${TASK_FILE}"
    # Copy to worktree
    cp "${MAIN_REPO}/${TASK_FILE}" "../${WORKTREE_NAME}/${TASK_FILE}"

    tmux send-keys -t "wm-issues:$WINDOW_NAME" "${NIX_CMD}@${TASK_FILE}'" Enter
fi

echo ""
if [ "$MODE" == "fix" ]; then
    echo "Agent spawned for fix on PR #${ISSUE_NUM} (branch: ${BRANCH_NAME})"
else
    echo "Agent spawned for issue #${ISSUE_NUM}"
fi

echo "Worktree: ${WORKTREE_PATH}"
echo "Tmux session: wm-issues"
echo "Tmux window: $WINDOW_NAME"
echo ""
echo "To attach: tmux attach -t wm-issues"
