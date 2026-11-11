# Agent Guide for wm-xcb

*How to contribute as an AI coding agent*

---

## Project Overview

wm-xcb is a new X11 window manager built in C with XCB. The architecture is designed for extensibility:
- **Components** that never modify each other's code
- **State machines** as the single authority on state mutations
- **Events** flowing through a central Hub
- Extensions that "just work" by coexisting without knowledge of each other

Reference: dwm, analyzed extensively for design patterns. A dwm checkout is not
part of this repository; if you keep one next to this checkout it is at
`../dwm/`.

Start with `.agents/skills/wm-bootstrap/SKILL.md`. It says where the tree
stands, maps the code, lists the gates, and names the traps that have already
cost time. This file holds the rules; that one holds the state.

---

## Key Documents

| Document | Purpose |
|----------|---------|
| `docs/README.md` | Doc index and the current state in a short section |
| `docs/PHILOSOPHY.md` | **The rules** — user promises, the four commitments, the state machine contract |
| `docs/TESTING.md` | Unit tests, Xephyr sessions, performance measurement |
| `docs/QUALITY.md` | The gates that run on commit, push, and CI — and how to add one |
| `.agents/skills/wm-bootstrap/SKILL.md` | Session bootstrap: state, code map, beads map, gates, traps. Load with `/skill:wm-bootstrap` |
| `.agents/skills/wm-5s/SKILL.md` | How to file harness friction as beads while you work |
| `.agents/skills/wm-review/SKILL.md` | Reviewing a `work/<bead>` branch: depth by module, verdict format, fix-or-record |
| `.agents/skills/wm-3m/SKILL.md` | Judging a run: muda, mura, muri — what the flow lost, how uneven it was, who was overburdened |
| `.agents/skills/wm-line/SKILL.md` | What to optimize and what to refuse: four rules, and the test for whether a borrowed technique applies here |
| `docs/VISION.md` | Why this project exists |
| `docs/architecture/README.md` | Index to the architecture documents |
| `docs/architecture/overview.md` | Core concepts and architecture |
| `docs/extensions/README.md` | Index to the extension designs |
| `plans/wm-architecture.md` | Phased implementation plan |
| `bd list --type=epic` | Current work breakdown |

`docs/README.md` and `docs/architecture/README.md` index the documents. Do not
copy an index into this file: a second copy drifts, and the copy an agent reads
first is the one that sends it to a path that no longer exists.

---

## The Project's Commitments

Read `docs/PHILOSOPHY.md` before designing anything. Four rules, checked in review:

1. **Components never call each other.** Requests and events through the hub only.
2. **State machines own state.** `sm_raw_write()` for reality, `sm_transition()`
   for intent. No direct assignment to a field that a machine tracks.
3. **Async first.** Nothing in the event loop may wait: no `_reply()` in handlers,
   no `waitpid()`/`read()` on a child, one flush per iteration, block in `poll()`
   or `xcb_wait_for_event()`.
4. **New behaviour is a registration, not an edit.** If a feature must edit a
   file it does not own, that file is missing an extension point. Add the
   extension point first and say so in the issue.

The point of all four is a promise to the person using the WM: behaviour does not
change behind their back when a new feature lands. Developer comfort is not the
goal; dwm is easier to read than this project and works today.

### Code says the thing, not the ticket

Never write a beads id in source, a script, a Makefile or a CI config. A reader
of the file does not have the tracker open, and an id carries no explanation: it
is a pointer to another system standing where the reason should be. Write the
reason, the constraint, the workaround, the behaviour that must not regress.
`scripts/check-bead-refs.sh` fails the build on one.

The same goes for names: no file, function or test named after a ticket. A test
called `test_wm_4f6_1_map_request` will outlive the ticket and mean nothing.

Documents may name the tracker where the tracker is the subject — a workflow doc,
a roadmap that maps phases to epics — but not where the id is a substitute for
the content.

### Do not invent limits

Write a constraint only when you can put a number and a source on it: the API
cost of a run, the context you have left, a command that blocks for N seconds,
a person who has to approve. "No time", "running low", "to keep this short",
"if it fits", "given the length" are not constraints; they are a decision to
stop early, and stopping early leaves a half-checked tree behind. Do the work or
name the specific thing that blocks it.

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

## Working on a Bead

Work is tracked in beads. GitHub is closed out: every issue there is closed,
every pull request merged, and new work starts as a bead.

```bash
bd ready                        # unblocked work, in priority order
bd show <bead-id>               # description, acceptance criteria, blockers
bd comments <bead-id>           # what an earlier review or attempt found
bd update <bead-id> --claim     # take it, so the next session sees it is taken
bd comment <bead-id> "..."      # what you found, for whoever continues
bd create --title "..." --acceptance "..."  # a new bead, with what done looks like
```

Read the comments before you plan. `bd show` prints them. An earlier review
may have left the finding that changes the design.

A bead is created with its acceptance criteria. `bd create
--acceptance` is the only write a worker may make to that field, and
`scripts/check-bead-acceptance.sh` fails the next commit on a live bead that has
none and no baseline records as debt. What a criterion names is
`.agents/skills/wm-bootstrap/SKILL.md`, "Writing one bead"; the recipe for a
friction bead is `.agents/skills/wm-5s/SKILL.md`, "How to file".

A bead that lists seam cases names the test file for each one, or says "no test
yet" after `grep -l` in `tests/x/`. Before the first edit, comment which of
those cases already have a file. The work is the cases with no file.

A bead's text outlives the tree it was written against. When work another bead
delivers replaces what this bead names, or a commit makes one of its criteria
impossible, the session that makes it stale corrects the bead's description or
criteria in the same session and names the commit or the file that took its
place. A comment is not a correction: `bd show` prints the description and the
criteria above the comments, so the next worker and then the reviewer each
decide again whether the bead has work. Where the session may not write the
bead's text, its comment says what the text should say, and the final message
repeats it for the session that closes the bead.

`bd edit` opens an editor and blocks an agent. Never use it.

One bead per branch is the usual case, named `work/<bead-id>`. Several beads
share a branch when they touch the same files: the branch is named for the first
id, and the prompt names them all. How a worker is spawned, and what
it may run, is `.agents/skills/wm-bootstrap/SKILL.md`, "Spawning workers". That
harness (`worker.py`) is not part of this repository.

A worker never pushes, merges, or closes its own bead. The commits stay on
`work/<bead-id>`, and a review decides what happens to them: see
`.agents/skills/wm-review/SKILL.md`.

### One instruction surface

`AGENTS.md` is the only file here that tells an agent how to work. `bd setup`
writes a second one for a coding agent, in two places: `.codex/config.toml` and
`.codex/hooks.json` (`.cursor/rules/beads.mdc` before them), whose hooks run
`bd codex-hook` so every session starts with `bd prime`'s own close protocol and
git policy; and a `BEGIN BEADS CODEX SETUP` block appended to `AGENTS.md`
itself, beside the tracked beads block at the end. `scripts/check-root.sh`
refuses the first, because `.codex` is off the root allowlist. It cannot see the
second, because `AGENTS.md` is on that allowlist, so the rule is the only guard.
Do not run `bd setup` for a coding agent here.

---

## Implementation Rules

### Code Style
- Follow existing patterns in the codebase
- The Makefile passes no `-std` flag, so the code compiles under the compiler's
  default dialect. What binds is `-pedantic` in the `make DEBUG=1` build: it
  rejects the GNU-only constructs that dialect would otherwise allow, so keep
  that build green
- Format with `clang-format --style=file` (`make format`)
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

### Where the Code Lives

The code map is the "Where the code lives" table in
`.agents/skills/wm-bootstrap/SKILL.md`. That table is the only copy, because a
copy here sent agents to `src/hub/` and `src/listeners/`, which do not exist.

Precedent to copy, module by module:

- `src/components/client-list.c` — a component: registration, XCB handlers, state
- `src/target/client.c` — a target: holds its state machines by name, owns the client list
- `src/xcb/xcb-handler.c` — the handler registry a component registers into
- `src/core/wm-hub.c` — registry, request router, event bus
- `src/core/wm-log.c` — logging

---

## Tests

Two suites, both run from the repository root:

```bash
make test                            # unit suite, prints "Total: N passed, M failed"
make test-x                          # seam suite: what the X server sees
```

Unit tests are `tests/unit/test-*.c`. A file registers its test group from a
constructor, so a file missing from `TEST_SRC` in the Makefile compiles nowhere
and its assertions never run. Run `scripts/check-test-src.sh`. The pre-commit and
pre-push hooks both run it. The seam tests are
`tests/x/test-*.sh`: they start a nested X server and assert against a live WM.

Copy the shape from `tests/unit/test-client-list-component.c` for a component
and `tests/unit/test-wm-tag.c` for a target. `docs/TESTING.md` has the debug
build, the seam harness, the sanitizer runs, and the performance measurement.

---

## What is checked, and where a rule lives

`scripts/gate-list.sh` prints every gate each hook runs, read out of
`lefthook.yml`, and `docs/QUALITY.md` has a row per gate saying what it proves. A
rule a command enforces is listed there and not repeated here: a second copy
goes stale, and a reader cannot tell a stale copy from the rule.

A rule no gate checks lives in the document or header where it applies, and
says there why no command checks it. These had no gate, so they left this file
for the place they apply. This is not every ungated rule: `docs/PHILOSOPHY.md`
section 3 lists the commitments and where each is checked, and that table is
not copied here. Delete a line below when a gate starts checking it.

- Components never include or call another component, and a header says what a
  function does and who owns what it allocates: `docs/architecture/component.md`.
- A walk over the client list follows `client_get_next()` and saves the next
  pointer before a callback that may free the current client:
  `src/target/client.h`.
- A document changes in the same commit as the behaviour it records, and an
  include is written from the repository root: `docs/QUALITY.md`, "Rules no gate
  checks".
- A new component or feature has a test that fails when the behaviour is
  absent: `docs/TESTING.md`, "Unit tests".
- One `xcb_flush()` per loop iteration: `docs/PHILOSOPHY.md` section 3.
- A new gate is proven by removing the exact thing it must catch, once per
  shape it must catch: `docs/QUALITY.md`, "Adding a gate".

---

## Passing a long message

A commit body or a bead comment with more than a line in it goes through stdin,
not through an argument: quoting and newlines survive that way, and a stray
backtick in a shell string does not get executed.

```bash
git commit -F - <<'EOF'
subject line

body, with `backticks` and $variables left alone
EOF

bd comment <bead-id> --stdin <<'EOF'
what you tried, what happened
EOF
```

A bead's description goes the same way, and there `--description` is the shape
that loses text: `bd update <id> --description ""` prints "Updated issue" and
exits 0, and a `$(...)` that produced nothing is indistinguishable from a
decision to clear the description. `--stdin` refuses an empty body, so a failed
read of the text cannot pass for the text:

```bash
bd update <bead-id> --stdin <<'EOF'
what changed, and what done means
EOF
```

A body of nothing but whitespace still gets through, which is why
`scripts/check-bead-descriptions.sh` fails the commit when an issue carries no
description. `bd history <bead-id> --events` keeps the text an update replaced,
under `Old:`, so a wipe is recoverable.

## When You're Done

The commit hook runs the gates in `lefthook.yml` for you (beads export, paths,
docs, test-src, bead refs, formatting, build, unit tests, async rule, commit
message). Do not skip them with `--no-verify`; if a gate fails, fix the cause.
If the hook is missing, run `make hooks` once.

1. Make sure tests pass: `make test`
2. If the change touches windows, focus, keys, or X properties, run the seam
   tests too: `make test-x`. They assert what X sees. A
   failure listed in `tests/quality/seam-baseline.txt` is known and does not
   fail the run; a failure missing from that file is new, exits non-zero, and
   is yours to fix or to baseline deliberately.
3. Run the static async check: `make check-async`. It must report zero new
   violations; a waiver (`allow-blocking: <reason>`) needs a real reason.
4. Format code: `make format`
5. Commit one topic: the subject says what changed, the body says why. A commit
   that names a tracker id instead of the reason is against the rule above.
6. Hand off in the final message: the files you changed, the commit, the
   `make test` result, the seam result if you ran it, the friction beads you
   filed, and anything you could not do.

A worker does not push and does not merge; what happens to the branch is a
review decision.

Never add quality checks by hand in a one-off command. `docs/QUALITY.md` has the
four-step recipe so the check runs for everyone from then on.

When a command, gate, script, or documented step does not work first time and you
need a workaround, file it: `.agents/skills/wm-5s/SKILL.md`. A workaround that
nobody records is paid for again by the next session. Parent epic `wm-dfg`,
label `friction`.

---

*For architectural questions, refer to `docs/architecture/`*
*For implementation guidance, refer to `docs/IMPLEMENTATION-ROADMAP.md`*

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:46cd31e7 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   bd dolt push
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->
