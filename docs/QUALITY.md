# Quality Gates

*Written 2026-11-01. Every gate below is a command. None of them consults a
model.*

> **Bottom line:** `lefthook.yml` is the list of gates and
> `.github/workflows/quality.yml` runs the same commands in CI. A gate is added
> in one place and called from both. If a check cannot be reduced to a command
> that gives the same answer twice, it does not belong here.

---

## Layers

| Layer | When | Time | Runs |
|-------|------|------|------|
| `pre-commit` | every commit | ~5 s | beads export, formatting, build, unit tests, async rule |
| `commit-msg` | every commit | instant | message rules (no AI attribution) |
| `pre-push` | every push | ~15 s | the above plus the `-pedantic` debug build |
| CI `gates` | every push and PR | ~2 min | `lefthook run pre-push` with the beads job excluded |
| CI `seam` | every push and PR | ~1 min | `make test-x` under `xvfb-run` — **advisory** |
| CI `sanitize` | every push and PR | ~2 min | `make SANITIZE=1 && ./test` — **advisory** |

Advisory means `continue-on-error: true`. They are wired and their output is
visible on purpose; they become required when they are green, which is tracked
in beads.

## The gates

| Gate | Command | What it proves |
|------|---------|----------------|
| build | `make -j$(nproc)` | the tree compiles |
| unit | `make test` | 645 assertions across hub, state machines, targets, components |
| format | `scripts/format-check.sh` | every C file matches `.clang-format`; reports, never rewrites |
| async | `scripts/check-async.sh` | no new blocking call in the event loop |
| commit-msg | `scripts/check-commit-msg.sh` | no `Co-Authored-By`, no "generated with", no robot footer |
| debug build | `make clean && make DEBUG=1 && make test` | the `-pedantic` build, which is what caught the zero-length array in `tiling.c` |
| seam | `make test-x` | modifiable X behaviour: mapped, tiled, focused, tags, EWMH (advisory) |
| sanitize | `make SANITIZE=1 && ./test` | memory and UB errors (advisory: the hub leaks 936 bytes) |

## Where the hooks live

The hook files are committed in `.githooks/` and each one is four lines: run
`lefthook run <hook name>`. So the gates are in the repository, not in one
machine's `.git`, and a worktree created by a worker gets the same ones.

Git cannot commit the pointer to that directory. `core.hooksPath` is local
config, which leaves one command per clone:

```bash
direnv allow        # or: nix develop
make hooks          # git config --local core.hooksPath .githooks
```

The two other hook owners stay out of the way: `lefthook install` would write
shims into `.git/hooks` and `bd hooks install` would take over
`core.hooksPath` for beads. Neither is needed, and `make hooks` is the only
install step.

If `lefthook` is not on PATH, the hook runs `nix develop --command lefthook run
<hook>`, so the gates run from the same config instead of a second copy of the
list. A machine with neither gets a warning and no gates; CI runs everything
regardless.

To skip the gates on purpose, for one commit:

```bash
WM_HOOKS=0 git commit -m "wip"
```

That is an escape hatch, not a habit: the same commit will still meet the gates
when it is pushed.

## The async baseline

`scripts/check-async.sh` fails on a blocking call unless it is waived or listed
in `tests/quality/async-baseline.txt`.

- **Waiver** — a comment `allow-blocking: <reason>` on the line or the two above
  it. For code that cannot block: startup paths before the loop runs.
- **Baseline** — the calls that are known and tracked in beads. Nine today, all
  in `bd list --parent wm-46k.2`.

A violation that is in neither fails. That is the point: the file only shrinks.
Adding an entry is a deliberate act:

```bash
scripts/check-async.sh --update-baseline
git diff tests/quality/async-baseline.txt    # read it before committing
```

Fixing a violation leaves a stale entry, which the script reports and which
should be removed with the same command. A stale entry is not an error, so
nobody is punished for improving the tree.

## Adding a gate

1. Write the check as a script under `scripts/` that exits non-zero on failure
   and prints `file:line` or the failing thing. No network, no model.
2. Add it to the right layer in `lefthook.yml` with a `fail_text` that says what
   to do about it.
3. Add the same command to the matching job in
   `.github/workflows/quality.yml`.
4. Add a row to the table above, and to `docs/TESTING.md` if a person would run
   it by hand.

If step 1 is not possible, the gate is not deterministic yet. That is a reason
to leave it out, not to add a heuristic.

## Running everything by hand

```bash
make hooks                  # once per clone; points git at .githooks/
lefthook run pre-push       # every gate that runs before a push
lefthook run pre-commit     # just the commit-time gates
LEFTHOOK_EXCLUDE=beads lefthook run pre-push   # without the beads export
make test-x                 # seam tests (needs a display; Xephyr handles that)
make SANITIZE=1 && ./test   # sanitizers
```

## What is deliberately not a gate

- **`make tidy` (clang-tidy).** Slow, needs `bear` and a full rebuild, and today
  it produces noise rather than findings. It belongs in CI as its own advisory
  job once someone has triaged the output.
- **`scripts/review-agent.sh`.** It spawns an LLM to review a PR against the
  architecture docs. Useful as a second opinion, useless as a gate: it can pass
  code that fails a test and fail code that is correct, and nobody can rerun it
  to the same answer. It stays advisory and is never listed in `lefthook.yml`.
- **Coverage percentages.** Not until the seam tests pass, otherwise the number
  measures the wrong thing.
