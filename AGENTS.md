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

A bead is created with its acceptance criteria, not given them later. `bd create
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

## ⚠️ TOP REPEATED MISTAKES (Read Before Starting)

These issues have appeared repeatedly across the merged pull requests:

### 1. Documentation Out of Sync with Code (25+ occurrences)
- **Symptom**: Comments/headers describe behavior that doesn't match implementation
- **Fix**: Update documentation in the SAME commit that changes behavior

```c
// BAD: Documentation says X but code does Y
/**
 * Resizes the widget to fit its contents
 */
void widget_set_size(Widget* w, int size) {
    // Actually ignores size parameter
    (void)size;
}

// GOOD: Documentation matches what the code actually does
/**
 * Sets the widget's minimum size (may be ignored if greater than max)
 */
void widget_set_size(Widget* w, int size);
```

### 2. Memory Leaks (15+ occurrences)
- **Symptom**: Allocated memory not freed, especially in shutdown/error paths
- **Fix**: Track ownership, free in all code paths. Document who owns the memory.

```c
// BAD: Leaks on error path AND on success path
void component_init(void) {
    Data* data = malloc(sizeof(Data));
    if (something_fails) return;  // LEAK!
    // Also: data leaked on shutdown if not freed
}

// GOOD: Document ownership and free on all exits
// Ownership: component owns 'data', caller must not free
void component_init(void) {
    Data* data = malloc(sizeof(Data));
    if (data == NULL) {
        LOG_ERROR("Failed to allocate data");
        return false;
    }
    // ... use data ...
    // On shutdown or error: free(data)
}

// GOOD: Ownership transferred to caller or stored
void component_init(Data** out_data) {
    Data* data = malloc(sizeof(Data));
    if (data == NULL) return false;
    // Ownership transferred to caller
    *out_data = data;
    return true;
}
```

### 3. Component Coupling Violations (12+ occurrences)
- **Symptom**: Components calling each other directly instead of through Hub
- **Fix**: All inter-component communication goes through hub_send_request() or hub_emit()

```c
// BAD: Direct coupling
void component_a_action(void) {
    component_b_do_something();  // NO!
}

// GOOD: Go through Hub
void component_a_action(void) {
    hub_send_request(REQ_COMPONENT_B_DO_SOMETHING, target_id);
}
```

### 4. State Machine Pattern Violations (10+ occurrences)
- **Symptom**: Bypassing SM for state changes, using wrong write type
- **Fix**: State changes always go through SM (raw_write for hardware, transition for requests)

```c
// BAD: Direct state modification
void on_configure(void) {
    client->fullscreen = true;  // NO!
}

// GOOD: Go through SM
void on_configure(void) {
    sm_raw_write(client->fullscreen_sm, FULLSCREEN_ENTERED);
}
```

### 5. Missing Test Coverage (8+ occurrences)
- **Symptom**: New components/features without unit tests
- **Fix**: Add tests BEFORE considering implementation done

```c
// REQUIRED: Test file for every new component
// test-wm-mycomponent.c
void test_my_component_init(void) {
    assert(my_component_init() == true);
    assert(my_component.registered == true);
}
void test_my_component_request(void) {
    hub_send_request(REQ_MY_ACTION, some_target);
    // assert expected state changes
}
```

A new check is proven by removing the exact thing it must catch, once per shape
it must catch: for a table walker, one row per shape — a plain row, a self-loop,
a row with a guard, a row with no event. A mutation that exercised a shape the
check already handled proves nothing about the one it does not. Run the proof
after the last edit to the test: a count taken before the test changed is the
previous test's count, and the comment that quotes it is wrong.

### 6. Dangling Pointers / Use-After-Free (8+ occurrences)
- **Symptom**: Using a pointer after the object it points to has been freed
- **Fix**: Snapshot pointers before callbacks that may free, null after free, check NULL before use

```c
// BAD: Iterator pointer invalidated by callback
for (Client* c = client_list_get_head(); c != NULL; c = client_get_next(c)) {
    callback(c);  // May free c!
    // c is now dangling
}

// GOOD: Snapshot next before callback
for (Client* c = client_list_get_head(); c != NULL;) {
    Client* next = client_get_next(c);
    callback(c);
    c = next;
}
```

The walk goes through `client_get_next()`, never `c->next`. The client list is
circular around a sentinel: the last client's `next` points at the sentinel, not
at NULL, so a loop that follows `c->next` until NULL never ends.

```c
// GOOD: Null pointer after free
void cleanup(Component* comp) {
    if (comp->data != NULL) {
        free(comp->data);
        comp->data = NULL;  // Prevent dangling use
    }
}
```

### 7. Include Path Inconsistency (6+ occurrences)
- **Symptom**: Using relative paths (`../../src/core/wm-hub.h`) instead of a path from the repository root
- **Fix**: Use an include the root `-I.` resolves (`#include "src/core/wm-hub.h"`)

```c
// BAD
#include "../../src/core/wm-hub.h"

// GOOD (matches rest of codebase)
#include "src/core/wm-hub.h"
```

### 8. Event Type Collisions (4+ occurrences)
- **Symptom**: Two components using the same event ID, so each receives the other's events, or an id past the event bus table, which is dropped
- **Fix**: Take the component's range from `src/event-types.h`. Do not pick a fresh number.

```c
// BAD: a fresh number can land on another component's event
enum ComponentEvents {
    EVT_TAG_CHANGED = 20,
};

// GOOD: start from the range this component owns
enum ComponentEvents {
    EVT_TAG_CHANGED = EVT_TAG_MANAGER_BASE + 0,
};
```

`hub_emit()` logs an error and drops a value at or above `HUB_MAX_EVENT_TYPES`. `tests/unit/test-event-types.c` fails when two listed constants share a value or a constant leaves its range. A new constant has to be added to that list, or the test does not see it.

### 9. API Contract Violations (6+ occurrences)
- **Symptom**: Functions don't do what comments say, ownership unclear
- **Fix**: Document ownership semantics, match implementation to docs

```c
// BAD: Ownership unclear
void client_set_title(Client* c, char* title);

// GOOD: Clear ownership semantics
/**
 * Sets client title.
 * Takes ownership of `title` - caller must not free after call.
 */
void client_set_title(Client* c, char* title);
```

### 10. Registration Without Verification (5+ occurrences)
- **Symptom**: Call hub_register_*() but don't check if it succeeded
- **Fix**: Always verify registration and handle failures

```c
// BAD
hub_register_component(&comp);
LOG_DEBUG("Component registered");  // May not be true!

// GOOD
hub_register_component(&comp);
if (!comp.registered) {
    LOG_ERROR("Component registration failed");
    return false;
}
```

### 11. Blocking the event loop

- **Symptom**: the WM stops responding to keys while something else happens; idle CPU is 100%
- **Fix**: nothing in the loop may wait. No `xcb_*_reply()` inside a handler or
  action, no `waitpid()`/`read()` on a child from a callback, one flush per
  iteration, and the loop blocks in `poll()` or `xcb_wait_for_event()`

```c
// BAD: a keybinding that runs dmenu freezes the whole WM until dmenu exits
bool launcher_action_launch(ActionInvocation* inv) {
    read(pipefd[0], cmd, sizeof(cmd) - 1);   // blocks the event loop
    waitpid(pid, &status, 0);                // blocks again
}

// GOOD: fork, return immediately, handle the result when it arrives
spawn_async(argv, on_launcher_done);         // completion arrives as an event
```

```c
// BAD: the loop spins, burning a core while nothing happens
while (running) handle_xcb_events();         // xcb_poll_for_event() returns NULL at once

// GOOD: block until there is something to do
while (running) { event = xcb_wait_for_event(dpy); dispatch(event); }
```

### 12. State changed outside a state machine

- **Symptom**: the WM and the X server disagree, or a subscriber never hears
  about a change that happened
- **Fix**: every tracked value changes through `sm_raw_write()` (reality) or
  `sm_transition()` (intent), and every transition emits exactly one event

```c
// BAD
void on_configure(Client* c) { c->fullscreen = true; }

// GOOD
void on_configure(Client* c) { sm_raw_write(client_get_sm(c, "fullscreen"), FULLSCREEN_STATE_ENTERED); }
```

### 13. A quality check that only ran once

- **Symptom**: someone ran a clever grep, a scan, or a manual review, found a
  problem, fixed it, and the check never ran again
- **Fix**: turn it into a gate. A script under `scripts/` that exits non-zero,
  listed in `lefthook.yml` and in `.github/workflows/quality.yml`, documented in
  `docs/QUALITY.md`. Gates are deterministic commands; an LLM review is a second
  opinion, not a gate.

---

### Passing a long message

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

---

## Lessons Learned (IMPORTANT)

### NEVER Kill Active Sessions

Before killing tmux sessions or windows, CHECK if an agent is actively working:
- `tmux list-windows` — see all windows
- `tmux capture-pane` — check if an agent is processing
- If uncertain, ASK the user first

### Worktrees Are Precious

Worktrees contain agent progress. Before removing or cleaning:
- Check `git status` — are there uncommitted changes?
- Check if agent is using the worktree
- If agent is working, DO NOT delete the worktree

### Overlapping Beads

Two beads that touch the same files conflict at the second merge: the merge
stops on a hand resolution that puts an unreviewed edit on a branch a reviewer
already approved. Which beads overlap, and what to do about it, is the
"Spawning workers" section of `.agents/skills/wm-bootstrap/SKILL.md`. The rule
lives there and not here: the copy that stood in this file read "nothing
shared, run them together", and that sentence dispatched three beads in parallel
whose branches shared four files.

---

*These lessons are here because future sessions will have no memory of past mistakes.*

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
