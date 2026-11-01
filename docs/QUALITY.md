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
| `pre-commit` | every commit | ~5 s | paths, docs, test-src, beads export, formatting, build, unit tests, async rule |
| `commit-msg` | every commit | instant | message rules (no AI attribution) |
| `pre-push` | every push | ~15 s | the above plus the `-pedantic` debug build, the standalone tests, and the build variants |
| CI `gates` | every push and PR | ~2 min | `lefthook run pre-push` with the beads job excluded |
| CI `seam` | every push and PR | ~1 min | `make test-x` under `xvfb-run`, required: known failures are baselined |
| CI `sanitize` | every push and PR | ~2 min | `make test SANITIZE=1` |

No CI job is advisory any more. The seam job could only become required once
`tests/x/run.sh` could tell a known failure from a new one; the baseline in
`tests/quality/seam-baseline.txt` is what makes its verdict mean something.

## The gates

| Gate | Command | What it proves |
|------|---------|----------------|
| build | `make -j$(nproc)` | the tree compiles |
| unit | `make test` | the unit suite across hub, state machines, targets, components |
| paths | `scripts/check-paths.sh` | no committed file names a home directory or a personal source tree |
| test-src | `scripts/check-test-src.sh` | every `test-*.c` with a `TEST_GROUP` is in `TEST_SRC`, so its assertions actually run |
| docs | `scripts/check-docs.sh` | documents name symbols and beads ids; no line references, no counts a command already prints, no completion percentage |
| format | `scripts/format-check.sh` | every tracked C file and header matches `.clang-format`; `--fix` rewrites the same list (`make format`) |
| async | `scripts/check-async.sh` | no new blocking call in the event loop |
| commit-msg | `scripts/check-commit-msg.sh` | no `Co-Authored-By`, no "generated with", no robot footer |
| debug build | `make clean && make DEBUG=1 && make test` | the `-pedantic` build, which is what caught the zero-length array in `tiling.c` |
| build variants | `scripts/check-build-variants.sh` | `make`, `make SANITIZE=1` and `make DEBUG=1` build in one tree without a clean, and each leaves its own binary in `./wm` |
| seam | `make test-x` | X behaviour: mapped, tiled, focused, tags, EWMH. Known failures come from `tests/quality/seam-baseline.txt`; a new one fails the run |
| sanitize | `make test SANITIZE=1` | memory and UB errors; no leak allowed |

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

## Why the gate scripts have no nix shebang

Both forms work, tested 2026-11-01 on x86_64-linux with Nix 2.36:

```sh
#!/usr/bin/env -S nix develop <path-to-repo> -c bash                 # works
#!/usr/bin/env nix-shell                                             # works
#! nix-shell -i bash -p lefthook
```

Neither is used for the gates, for three reasons:

- The `nix-shell -p lefthook` form resolves packages from the channel, not from
  the flake: it picked lefthook 2.1.14 while the dev shell has 2.1.15, and it
  has none of the build dependencies (`clang-format`, `pkg-config`, the libxcb
  family), so `make` inside it fails. It drifts from the flake by construction.
- The `nix develop` form needs a path or a working directory in the shebang.
  Hooks run from the repo root, so it would work there, but the same script
  called from another directory would fail to find the flake, and a machine
  without Nix would fail to exec the hook at all instead of warning and
  continuing.
- The gates already run inside the dev shell when it is active. A shebang makes
  the second entry re-enter `nix develop` on every gate, which is a second of
  flake evaluation per script for no benefit.

`scripts/run-hook.sh` keeps the decision in one place: lefthook if present,
otherwise `nix develop --command lefthook`, otherwise a warning.

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
make test SANITIZE=1        # sanitizers (builds the test binary too)
scripts/check-build-variants.sh   # release, sanitize and debug builds in one tree
```

## What is deliberately not a gate

- **`make tidy` (clang-tidy).** Slow, needs `bear` and a full rebuild, and today
  it produces noise rather than findings. It belongs in CI as its own advisory
  job once someone has triaged the output.
- **Reviews, by a human or an agent.** They catch what a check cannot express —
  a criterion quietly reinterpreted, a doc that now lies, a design choice that
  will cost later — and they are useless as a gate: a review does not answer the
  same way twice, and nobody can rerun it to the same answer. `.agents/skills/wm-review/SKILL.md`
  is the procedure; its findings become a commit (which the gates then judge) or
  a bead comment. Never a `lefthook.yml` job.
- **Coverage percentages.** Not until the seam tests pass, otherwise the number
  measures the wrong thing.
