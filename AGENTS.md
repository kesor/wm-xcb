# Agent Guide for wm-xcb

*How to contribute as an AI coding agent*

---

## Project Overview

wm-xcb is a new X11 window manager built in C with XCB. The architecture is designed for extensibility:
- **Components** that never modify each other's code
- **State machines** as the single authority on state mutations
- **Events** flowing through a central Hub
- Extensions that "just work" by coexisting without knowledge of each other

Reference: dwm (`../dwm/`) — analyzed extensively for design patterns

---

## Key Documents

### Start Here

| Document | Purpose |
|----------|---------|
| `docs/PRE-WORK_CHECKLIST.md` | Initial handoff context |
| `docs/GRILL-ME-PROMPT.md` | Design decisions made in grill session |
| `docs/architecture/overview.md` | Core concepts and architecture |
| `plans/wm-architecture.md` | Phased implementation plan |
| `.github/ISSUE_TEMPLATE/prd.yml` | Product Requirements Document |

### Architecture Documents

```
docs/architecture/
├── README.md           # Index to architecture docs
├── overview.md         # Core concepts, event/request flows
├── hub.md             # Hub: registry, router, event bus
├── state-machine.md    # State machine framework
├── component.md       # Component design
├── target.md          # Target design (Client, Monitor)
└── decisions.md       # Decision log with rationale
```

### Extension Designs

```
docs/extensions/
├── README.md     # Index to all extensions
├── gap.md        # Gaps between tiled windows
├── pertag.md     # Per-tag layouts
├── floatpos.md   # Rule-based floating position
├── urgency.md    # Urgency hints
├── bstack.md     # Bottom-stack tiling
├── cfact.md      # Per-client relative sizing
└── ... (13 extensions total)
```

---

## The Architecture in Brief

### Core Concepts

- **Hub** — Central orchestrator with registry, router, and event bus
- **Component** — Self-contained driver with executor (handles requests) and XCB handlers
- **State Machine** — Tracks mutually exclusive state, lives on targets (Client, Monitor)
- **Target** — Entity that owns state machines (window = Client, display = Monitor)

### Two Types of Writers

1. **Raw Writer** — Authoritative state change (hardware events). No guards.
2. **Request Writer** — Intent to change state (keybindings, plugins). Guards apply.

### XCB Events vs Hub Events

- **XCB events** (keyboard, mouse, RandR): Components own handlers, called directly from event loop
- **Hub events** (EVT_*): Flow through hub's event bus, components subscribe

### Components Own Their Handlers

Each component handles its own XCB events:
- `keybinding.c` handles KEY_PRESS/RELEASE
- `focus.c` handles ENTER_NOTIFY/FOCUS_IN
- `monitor-manager.c` handles RANDR_NOTIFY

---

## How to Start an Agent

### Spawn a New Agent

Use the spawn script from the main repo directory:

```bash
cd ~/src/suckless/wm
./scripts/spawn-agent.sh <issue_number>
```

This:
1. Creates a git worktree from the main repo
2. Copies docs, plans, and vendor to the worktree
3. Opens a new tmux window
4. Runs pi with the task file

### Continue/Fix a PR

When code review comments are added to a PR:

```bash
cd ~/src/suckless/wm
./scripts/spawn-agent.sh <PR_number> --fix
```

This:
1. Finds the existing worktree for the PR's branch
2. Fetches PR comments from GitHub
3. Creates a fix task file
4. Resumes the pi session (or starts new if needed)

### Continue Existing Work

```bash
cd ~/src/suckless/wm
./scripts/spawn-agent.sh <issue_or_pr> --continue
```

---

## Working on an Issue

### 1. Read the Task File

Each issue has a task file: `task-issue-<N>.txt`

```bash
cat task-issue-2.txt
```

### 2. Read the Architecture

Before implementing, read:
```bash
docs/architecture/overview.md
docs/architecture/hub.md
docs/architecture/state-machine.md
docs/architecture/component.md
docs/architecture/target.md
```

### 3. Check the Implementation Plan

```bash
plans/wm-architecture.md
```

### 4. Look at Existing Code

Follow existing code style:
```bash
wm-window-list.c    # Linked list patterns
wm-log.c           # Logging patterns
wm-states.c        # Existing state machine (for reference)
```

---

## Implementation Rules

### Code Style
- Follow existing patterns in the codebase
- Use C99 with `-std=c99 -pedantic`
- Format with `clang-format --style=file`
- Use `LOG_DEBUG()`, `LOG_ERROR()`, `LOG_FATAL()` for logging

### State Machines
- SMs live on targets, not components
- Templates define states/transitions
- Raw writes for authoritative changes (hardware events)
- Transitions for requests (may fail guards)

### Components
- Register with hub at init
- Own XCB handlers for their domain
- Provide SM templates that targets adopt
- Emit events through hub when SMs transition

### Git Workflow
```bash
# Create branch
git checkout -b issue-<N>-<description>

# Commit with issue reference
git commit -m "feat(hub): implement component (Issue #N)"

# Push
git push origin issue-<N>-<description>
```

### Creating PRs
```bash
gh pr create \
  --repo kesor/wm-xcb \
  --base master \
  --title "feat(hub): implement component (Issue #N)" \
  --body "Implements GitHub issue #N: Title

## Changes
- List of changes

## Testing
- How to test"
```

---

## Directory Structure

```
wm/
├── src/
│   ├── hub/           # Hub implementation (future)
│   ├── sm/            # State machine framework (future)
│   ├── components/    # Built-in components (future)
│   └── listeners/     # Event listeners (future)
├── docs/
│   ├── architecture/  # Architecture documentation
│   └── extensions/    # Extension designs
├── scripts/
│   └── spawn-agent.sh # Spawn agent script
├── plans/
│   └── wm-architecture.md  # Implementation plan
└── (existing code)
```

---

## Test Files

Write tests following existing patterns:
```bash
test-wm-window-list.c    # Existing test example
```

Run tests:
```bash
make clean && make test
```

---

## Useful Commands

### Check GitHub Issues
```bash
gh issue list --repo kesor/wm-xcb --state open
```

### Check PR Status
```bash
gh pr list --repo kesor/wm-xcb --state open
```

### Fetch PR Comments
```bash
gh api repos/kesor/wm-xcb/pulls/<PR>/comments --jq '.[] | .body'
```

### Attach to tmux Window
```bash
tmux attach -t issue-<N>
```

### List Active tmux Windows
```bash
tmux list-windows | grep issue
```

---

## Key GitHub Links

- **Repo**: https://github.com/kesor/wm-xcb
- **Issues**: https://github.com/kesor/wm-xcb/issues
- **PRs**: https://github.com/kesor/wm-xcb/pulls
- **Kanban Board**: https://github.com/users/kesor/projects/1

---

## When You're Done

1. Make sure tests pass: `make test`
2. Format code: `clang-format --style=file -i *.c *.h`
3. Commit with meaningful message
4. Push to your branch
5. Create PR referencing the issue

---

*For architectural questions, refer to `docs/architecture/`*
*For implementation guidance, refer to `plans/wm-architecture.md`*

---

## Lessons Learned (IMPORTANT)

### NEVER Kill Active Sessions

Before killing tmux sessions or windows, CHECK if an agent is actively working:
- `tmux list-windows` — see all windows
- `tmux capture-pane` — check if agent is processing
- If uncertain, ASK the user first

### Worktrees Are Precious

Worktrees contain agent progress. Before removing or cleaning:
- Check `git status` — are there uncommitted changes?
- Check if agent is using the worktree
- If agent is working, DO NOT delete the worktree

### The spawn-agent Script Has Quirks

- Use `git fetch origin` at start, create worktrees from `origin/master`
- Worktrees are in `~/src/suckless/wm-issue-N/`
- The tmux session is `wm-issues` with windows named `issue-N` or `pr-N`
- nix develop needs to run from `~/src/suckless` (has flake.nix), then cd to worktree
- Session name in pi is verbose — use `tmux attach -t wm-issues` then select window

### File Conflict Prevention

Phase 1 (Hub) should have been ONE branch — both registry (#2) and event bus (#3) modified the same files. Future parallel work should:
- Check what files unmerged PRs modify
- Avoid starting work that touches the same core files
- Or establish single-writer ownership for core files

---

*These lessons are here because future sessions will have no memory of past mistakes.*
