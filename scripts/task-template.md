# Task: {TITLE}

## IMPORTANT

- You are in a git worktree at ~/src/suckless/wm-issue-{ISSUE}/
- All work MUST be done in this directory, NOT in ~/src/suckless/wm
- Your branch is: {BRANCH_NAME}

## Steps

1. Run: gh issue view {ISSUE} --repo kesor/wm-xcb
2. Read the PRD at github.com/kesor/wm-xcb/issues/1
3. Read architecture docs in docs/architecture/
4. Implement the issue (write code in CURRENT directory)
5. When complete: make check && make test
6. Push your branch: git push -u origin HEAD:{BRANCH_NAME}
7. Write PR description to a FILE (e.g., pr-body.md), then verify it looks correct
8. Create PR: gh pr create --repo kesor/wm-xcb --base master --title "feat(...)" --body-file pr-body.md

## Development Environment

The dev environment is automatically set up.

## Notes

- DO NOT use `git add -A` - add only the files you modified
- DO NOT commit pr-body.md to git
- Use HEAD:{BRANCH_NAME} to explicitly push your branch (not master)
