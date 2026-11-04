---
name: wm-bootstrap
description: Bootstrap any session on the wm-xcb window manager (this repository). Use as the first step when starting or resuming work here, when asked what the state of the project is, when choosing which beads issue to implement, or when spawning worker agents on beads issues.
---

# wm-xcb bootstrap

Load this first. It says where the project stands, what to read, what enforces
quality, how the work is organised, and the traps that have already cost
time. It does not repeat the documents it points at.

## What this project is, honestly

An X11 window manager in C over libxcb, built as a dwm alternative where a
feature is a component that subscribes to events and sends requests, never a
patch to shared functions. State lives in state machines on targets.

The framework is built and unit-tested, and windows reach the screen: a window
is mapped on `MapRequest` with a border and `WM_STATE=Normal`, and the visible
clients of the selected monitor are tiled. What is still missing is keys, the X
input focus, and the EWMH root properties. So "make it work" means the X11
output path, which is epic `wm-4f6` and the top of the priority list.

`docs/README.md#current-state` is the list of what does and does not work, and
each gap is a beads issue. Do not restate it here, and do not call the project
finished: this paragraph said windows were never mapped long after they were,
because it was a second copy of that section.

## Start here

```bash
direnv allow          # or: nix develop
make hooks            # once per clone: points git at .githooks
make && make test     # the unit suite must pass
bd ready              # what can be picked up, unblocked
```

Then read, in this order:

| Document | Why |
|----------|-----|
| `.agents/skills/wm-5s/SKILL.md` | how to file harness friction while you work |
| `docs/PHILOSOPHY.md` | the four commitments and the rules that follow from them |
| `docs/architecture/overview.md` | hub, targets, components, two kinds of write |
| the architecture doc your issue names | e.g. `state-machine.md`, `xcb-integration.md` |

For testing mechanics, `docs/TESTING.md`. For the gate list, `docs/QUALITY.md`.

## Where the code lives

| Path | Contents |
|------|----------|
| `wm.c` | entry point: init order, event loop, shutdown order |
| `wm-hub.c/.h` | registry, request router, event bus |
| `src/sm/` | state machine framework: templates, instances, guards, hooks |
| `src/target/` | `Client`, `Monitor`, `Tag`; each owns its state machines |
| `src/components/` | client-list, focus, fullscreen, keybinding, monitor-manager, pertag, tag-manager, tiling |
| `src/actions/` | action registry, keybinding bindings, launcher, terminal |
| `src/xcb/` | XCB handler registry; components register their handlers |
| `wm-xcb.c` | connection, root window, startup rescan, event loop |
| `config*.h`, `config.c` | compiled-in configuration and keybinding wiring |
| `tests/x/` | seam tests: assert what the X server sees |
| `scripts/` | gates and harnesses, each runnable by hand |

## The four commitments, and what enforces them

1. **Components never call each other.** Requests and events through the hub.
2. **State machines own state.** `sm_raw_write()` for reality (an X event said
   so), `sm_transition()` for intent (a keybinding asked). No field assignment.
3. **Nothing in the event loop may wait.** No `_reply()` in handlers, no
   `waitpid()`/`read()` on a child, block in `poll()`/`xcb_wait_for_event()`.
4. **New behaviour is a registration, not an edit.** If a feature must edit a
   file it does not own, that file is missing an extension point; add it first.

docs/QUALITY.md is the gate list: one row per gate, the command, and what it
enforces. This file does not repeat it. A second copy drifts, and the copy a
session reads first is the one that sends it to a command that no longer exists -
that is how the row here still said `make test` a commit after the hook switched
to `scripts/check-unit.sh`.



`make test-x` does not fail by design. `tests/x/run.sh` compares the failures it
saw against `tests/quality/seam-baseline.txt`: an assertion listed there is
reported as a known failure and does not change the exit status, and an
assertion missing from there is new and exits non-zero. The summary line says
`N passed, M known failed, K new failed`, and `K` is the number to act on. Do
not weaken a test to make it green; fix the defect, or accept today's failures
with `make test-x TEST_X_ARGS="--update-baseline"` and say in a bead which
defect the new entry stands for.

## Work organisation (beads)

Work lives in beads, not in GitHub issues (GitHub is closed out and points
here). Epics, as of the last commit:

| Epic | Covers |
|------|--------|
| `wm-4f6` **P0** | X11 output path: map, place, tile, focus, keys — the blocker for everything visible |
| `wm-9n0` | test and measurement harness |
| `wm-46k` | making the commitments enforceable (state machine ergonomics, async audit) |
| `wm-lle` | usable without a compiler: runtime config, defaults, visible errors |
| `wm-rh6` | floating, urgency, pointer drag, bar |
| `wm-m9n` | extension designs |
| `wm-utz` | docs and tracker hygiene |

Verify with `bd list --type=epic`; do not trust this table after the first new
epic.

Per issue:

```bash
bd show <id>                     # description + acceptance criteria + blockers
bd update <id> --claim           # or: --status=in_progress
# ... work, commit ...
```

The cycle rebases onto the merge ref before the review, so the reviewer reads
that rebase's result. Merge rebases again, and that second rebase is a no-op
unless the merge ref has moved since. The merge then refuses two states, and
neither refusal is a gate failure. A `review-<slug>` window still open means
the review is still running: wait for it, then merge. A HEAD that is not the
sha in the last `Approved at <sha>` verdict, read before that second rebase,
means that approval does not cover this commit: review again before merging.
The merge closes the bead. A worker stops at in_progress: see "Definition of
done".

Rules: read the acceptance criteria before starting. A parent epic is not a
task. `bd edit` opens an editor and blocks an agent — never use it.

## Implementing one bead

1. Read `bd show <id>` and the docs it names.
2. If the change is observable through X, write or extend a `tests/x/test-*.sh`
   first; use `require_visible` for preconditions so a test cannot pass because
   the window was never there.
3. Implement in the component that owns the concern. Add a state machine
   transition rather than a field write; add a request rather than a direct
   call.
4. Run the gates: `make test`, `make test-x`, `make check-async`,
   `scripts/format-check.sh`. Fix causes, not symptoms.
5. Commit one topic: subject says what changed, body says why. Leave the bead
   in_progress and say in the final message what is done; the merge closes it.

## Spawning workers (optional)

A local harness (`worker.py`, not part of this repository) runs one worker per git
worktree inside a tmux window with a `pi` agent. `auto <bead-id> [--merge]`
starts a run, waits, reviews it on a different model family, and notifies;
`inbox` shows what is in flight with the verdict; `review` runs the review
alone. `worker.py help` is the command list; this file does not repeat it,
because a copy here rots while the real one changes.

It lives beside the main checkout in a directory named `ai-workers-for-wm/`, so
from a worktree created under `.worktrees/<bead>/` it is
`../../../ai-workers-for-wm/`. It is not on `PATH` and not in this tree: run it
as `./worker.py <command>` from that directory.

`worker.py`, `README.md`, `HANDOFF.md` and `PROCESS.md` in that directory are
safe to read: the flags, the defaults, the worktree layout and the rules for the
session that dispatches are all written there. They are the harness's own
project, so a worker reads them and does not edit them, and this repository never
gets a copy of one. A fact a document here needs — a flag, a default, a prompt
line — is read from those files. If it cannot be confirmed there, ask for it in
the final message instead of writing it from memory: a wrong line in a document
about the harness is the one mistake a worker cannot check.

Two things it cannot decide for you:

- **One bead per worker, run in parallel.** Small batches review better: a
  single bead is a ten-minute review, five beads was an eighteen-minute review
  at 70% of the reviewer's context. `worker.py auto <bead>` as many times as
  there are disjoint beads, staggered a minute or two apart.
- **Overlapping files decide what "disjoint" means.** `worker.py overlap <id>
  <id> ...` reads each bead's description and comments for file paths, and, when
  a worktree exists, the diff on that branch. It prints the intersection. A
  file it prints is in both sets, which is not proof both branches will edit
  it. A non-empty answer still means serialise. An empty answer proves nothing,
  because a bead written before the work exists names almost no path. Two beads
  also overlap when they are in the same epic. An empty answer is what
  dispatched three beads in one epic whose branches shared files the tool did
  not print. They also overlap when both name the same component under
  `src/components/`, by the component's own name (`tiling`, `client-list`,
  `focus`, `monitor-manager`, ...) and not by a path. Overlapping means run
  them in order, or give one worker several ids in one call: the second merge
  otherwise stops on a rebase conflict that a person resolves by hand, which
  puts an unreviewed edit on a branch the reviewer already approved. A
  serialised dispatch costs ten minutes (the harness's `PROCESS.md`, "Stagger,
  and check the overlap first").
- **Review.** Workers commit to `work/<bead-id>` and never push, merge, or close
  a bead. Merging is a human decision.

## Friction is a bug in the harness

When a command, gate, documented step or tool does not work first time and you
need a workaround, file a bead instead of working around it silently. Read
`.agents/skills/wm-5s/SKILL.md`: it has the 5S bucket, the title shape, the
four-field template, and the parent epic `wm-dfg`. File, then continue; list the
beads in your final message. This is how the traps below stop being traps.

## Traps that already cost time

- **Xephyr has no usable fonts.** `xterm` dies with "cannot load font". Use
  `tools/xclient` (`make tools/xclient`) to create test windows.
- **A hand-started `Xvfb` takes `-screen 0 WxHxD`, not `-screen WxHxD`.**
  `Xvfb :7 -screen 800x600x24` prints its usage line and exits, and the reason
  is in the log, not on stderr. The working form is the one
  `scripts/lib/private-display.sh` already uses:
  `xvfb-run -a -s "-screen 0 1280x800x24" <command>`.
- **`make DEBUG=1` is the only way to read the event log**, and `-pedantic`
  there is what caught a zero-length array. Keep that build green.
- **`xdotool search --onlyvisible`** is the quickest "did the WM map it" check.
- **`pkill -f <pattern>` kills the shell that runs it** when the pattern appears
  in its own command line: two self-kills, exit code 143, on one branch, and one
  of them took the log grep with it and produced a wrong conclusion. Use
  `pkill -x <name>`, `tmux kill-window -t wm:<i>`, `pgrep -af` to look first, or
  the pid.
- **Seam tests pass for the wrong reason easily.** "The window is hidden now"
  must not be satisfied by a window that was never shown.
- **`.gitignore` has `test-*`**, which silently ignores new test scripts unless
  a `!` rule re-includes them.
- **`git checkout -- <file>` restores from the index, not HEAD.** After staging,
  use `git checkout HEAD -- <file>`.
- **Event ids share one table.** A component takes the next free range in
  `src/event-types.h` and starts its enum from that base. A fresh integer can
  land on another component's event. `hub_emit()` logs and drops a value at or
  above `HUB_MAX_EVENT_TYPES`.
- **Adoption happens when a target is created**, so init order decides which
  components a monitor or client adopts (`wm.c`).
- **No AI attribution anywhere**: no `Co-Authored-By`, no "generated with", no
  robot footer. The git author is the only attribution.
- **No personal paths**: this repository is public. `scripts/check-paths.sh`
  fails on `/home/<user>` and `~/src`-style paths.
- **`bd setup <tool>` writes its own copy of the beads workflow.** The one in
  `AGENTS.md` is the one that stays; `bd setup cursor` had put a second block in
  `.cursor/rules/beads.mdc` with a git policy that contradicts this repository's
  harness, and it is gone. Running `bd setup` for a tool nobody here uses adds a
  copy back, and `bd setup <tool> --check` reports an incomplete integration
  after `--remove` as well as before the first install, so it cannot tell you
  whether to run the setup or to leave it alone.
- **A merge can end up on no ref.** The publish step pushes, then resets master
  onto `origin/master`, so a merge made while that push was in flight is in
  neither and the reset takes it away. The reset is `git reset --hard
  origin/<MERGE_REF>` in the harness's `wpy/commands/publish.py`, and it runs
  whether or not master moved during the wait. That wait is the push, the check
  run and the merge, so it runs to minutes: the 2026-11-03 publish that
  discarded a merge recorded 298612ms on its `phase=publish` row. A merge made
  into that gap is discarded from master. `land` writes `refs/kept/<bead>`
  before it deletes the branch, and the reset does not move that ref, so a
  merge that went through `land` is not only an object. When that ref is
  missing, the commit exists only in the reflog and the next `git gc` removes
  it. No commit on a `work/<bead>` branch can guard the reset: that file is not
  in this repository. `HANDOFF.md` section 5 rule 3 says `worker.py` is not
  edited while a cycle is in flight, and that anything else waits for a
  boundary; this skill already says a worker reads the harness and does not
  edit it. A bead asking for that guard is a session-between-runs job, so
  record the finding and spend no turns looking for an in-tree fix. When a
  bead is closed as merged and its files are not on master, read
  `refs/kept/<bead>` first, else `git reflog master` for its
  `merge work/<bead>` line, pin a reflog sha with
  `git update-ref refs/local/recovered-<bead> <sha>`, then merge it back and
  say so in the bead.
- **Do not push, and do not rewrite published history,** unless asked.

## Definition of done

- Acceptance criteria from `bd show` are met, not approximated.
- `make test` green; `make check-async` shows no new violations;
  `make test-x` no worse than before; formatting clean.
- The worker leaves the bead in_progress and says in its final message what is
  done and what is not. The bead is closed, and the tracker synced, by the
  merge: it happens after the review, and neither is a worker's step.
- Anything deliberately left out is stated in the final message, not implied.
