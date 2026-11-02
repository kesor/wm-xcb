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
input focus, and the EWMH root properties, and the event loop polls instead of
blocking. So "make it work" means the X11 output path, which is epic `wm-4f6`
and the top of the priority list.

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

| Gate | Command | Enforces |
|------|---------|----------|
| build | `make` | it compiles |
| unit | `make test` | the unit suite |
| seam | `make test-x` | what X sees: mapped, tiled, focused, tags, EWMH; a failure missing from `tests/quality/seam-baseline.txt` fails the run |
| async | `make check-async` | no new blocking call (baseline in `tests/quality/`) |
| paths | `scripts/check-paths.sh` | no home directory or personal source tree in a public repo |
| format | `scripts/format-check.sh` | clang-format clean; `make format` fixes |
| commit-msg | `scripts/check-commit-msg.sh` | no AI attribution trailers |
| debug | `make clean && make DEBUG=1` | the `-pedantic` build stays green |

The hooks in `.githooks/` run these; CI runs the same commands.

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

The merge runs after the review; it rebases, closes the bead and syncs the
tracker through `refs/dolt/data`. A worker stops at in_progress: see
"Definition of done".

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

A local harness (`worker.sh`, not part of this repository) runs one worker per git
worktree inside a tmux window with a `pi` agent. `auto <bead-id> [--merge]`
starts a run, waits, reviews it on a different model family, and notifies;
`inbox` shows what is in flight with the verdict; `review` runs the review
alone. `worker.sh --help` is the flag list; this file does not repeat it, because
a copy here rots while the real one changes.

Two things it cannot decide for you:

- **One bead per worker, run in parallel.** Small batches review better: a
  single bead is a ten-minute review, five beads was an eighteen-minute review
  at 70% of the reviewer's context. `worker.sh auto <bead>` as many times as
  there are disjoint beads, staggered a minute or two apart.
- **Overlapping files decide what "disjoint" means.** `worker.sh overlap <id>
  <id> ...` reads each bead's description and comments for file paths and says
  which are shared. Nothing shared: run them in parallel. Shared: run them in
  order, or give one worker several ids in one call — the second merge stops on
  a rebase conflict that a person resolves by hand, which puts an unreviewed
  edit on a branch the reviewer already approved.
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
- **Event ids are a shared number space** with no per-component namespace:
  `EVT_TAG_CHANGED` collides with `EVT_FULLSCREEN_ENTERED` today, and the hub
  drops anything above 63 silently.
- **Adoption happens when a target is created**, so init order decides which
  components a monitor or client adopts (`wm.c`).
- **No AI attribution anywhere**: no `Co-Authored-By`, no "generated with", no
  robot footer. The git author is the only attribution.
- **No personal paths**: this repository is public. `scripts/check-paths.sh`
  fails on `/home/<user>` and `~/src`-style paths.
- **Do not push, and do not rewrite published history,** unless asked.

## Definition of done

- Acceptance criteria from `bd show` are met, not approximated.
- `make test` green; `make check-async` shows no new violations;
  `make test-x` no worse than before; formatting clean.
- The worker leaves the bead in_progress and says in its final message what is
  done and what is not. The bead is closed, and the tracker synced, by the
  merge: it happens after the review, and neither is a worker's step.
- Anything deliberately left out is stated in the final message, not implied.
