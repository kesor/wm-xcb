# Quality Gates

*Written 2026-11-01. Every gate below is a command. None of them consults a
model.*

> **Bottom line:** `lefthook.yml` is the list of gates and
> `.github/workflows/quality.yml` runs the same commands in CI. A gate is added
> in one place and called from both. If a check cannot be reduced to a command
> that gives the same answer twice, it does not belong here.
>
> CI calls `lefthook run pre-push --force`. Without `--force` lefthook computes
> the files a push touched, and a CI checkout has no push range: the branch tip
> is the commit already checked out, the file list is empty, and lefthook skips
> every command and reports success. The claim that CI runs the same commands
> was false until the flag was there. The job's runtime is the check: tens of
> seconds means the gates ran.

---

## Layers

| Layer | When | Time | Runs |
|-------|------|------|------|
| `pre-commit` | every commit | ~5 s | paths, docs, test-src, bead refs, beads export, formatting, build, unit tests, async rule |
| `commit-msg` | every commit | instant | message rules (no AI attribution) |
<<<<<<< HEAD
| `pre-push` | every push | ~65 s | the above plus the flake evaluation, the seam suite, the `-pedantic` debug build, the standalone tests, the build variants, and the harness variants |
||||||| parent of e4bad (drop the CI sanitize job, which ignored the leak baseline)
| `pre-push` | every push | ~15 s | the above plus the `-pedantic` debug build, the standalone tests, the build variants, and the harness variants |
=======
| `pre-push` | every push | ~50 s | the above plus the `-pedantic` debug build, the standalone tests, the build variants, the harness variants, and the sanitizers |
>>>>>>> e4bad (drop the CI sanitize job, which ignored the leak baseline)
| CI `gates` | every push and PR | ~2 min | `lefthook run pre-push --force` with the beads job excluded |
| CI `seam` | every push and PR | ~1 min | `make test-x` under `xvfb-run`, required: known failures are baselined |
| CI `sanitize-x` | every push and PR | ~1 min | `scripts/check-sanitize-x.sh` under `xvfb-run`: the WM in a nested server under ASan and UBSan |

No CI job is advisory any more. The seam job could only become required once
`tests/x/run.sh` could tell a known failure from a new one; the baseline in
`tests/quality/seam-baseline.txt` is what makes its verdict mean something.
`lefthook.yml` runs the same suite before every push, in the `seam` job.

The two nested-server jobs upload their logs as artifacts, both from under
`/tmp`. `nix develop` points `TMPDIR` at a private per-shell directory and the
harnesses write their logs under `$TMPDIR`, so the seam job pins `TMPDIR=/tmp`
on its run line and `scripts/check-sanitize-x.sh` pins it on the harness
invocation. The upload fails the job when that directory is empty: an artifact
step that quietly uploads nothing removes the evidence from exactly the run
that needs it.

<<<<<<< HEAD
The sanitizer coverage is split the same way the code is. The unit suite runs
with no X connection, and the map, tile and focus requests return before they
are built without one, so the `sanitize` gate inside the gates job cannot see
them: a 2-byte value passed where XCB reads 4 left every gate green and aborted
the WM on the first `MapRequest`. The `sanitize-x` job covers that half by
running a real WM in a nested server under ASan and UBSan. It is not in
`lefthook.yml` because it rebuilds the tree under the sanitizers and takes about
a minute; the seam suite it sits next to is a push gate, and needs no display of
its own because `scripts/check-seam.sh` starts Xvfb when `DISPLAY` is unset.
||||||| parent of e4bad (drop the CI sanitize job, which ignored the leak baseline)
The `sanitize` and `sanitize-x` jobs cover different code because they can
reach different code: the unit suite runs with no X connection, and the map,
tile and focus requests return before they are built without one. A 2-byte
value passed where XCB reads 4 left every gate green and aborted the WM on the
first `MapRequest`. `sanitize-x` is not in `lefthook.yml` because it needs a
display and a nested X server, the same reason the seam tests are a CI job
rather than a hook.
=======
The `sanitize` gate and the `sanitize-x` job cover different code because they
can reach different code: the unit suite runs with no X connection, and the map,
tile and focus requests return before they are built without one. A 2-byte
value passed where XCB reads 4 left every gate green and aborted the WM on the
first `MapRequest`. `sanitize-x` is not in `lefthook.yml` because it needs a
display and a nested X server, the same reason the seam tests are a CI job
rather than a hook.
>>>>>>> e4bad (drop the CI sanitize job, which ignored the leak baseline)

The unit suite under the sanitizers is the `sanitize` gate in the `gates` job,
not a job of its own. A job that ran `make test SANITIZE=1` raw was red from the
first push: the suite leaks at allocation sites that
`tests/quality/asan-baseline.txt` records, and only `scripts/check-sanitize.sh`
reads that baseline. Two places asked the same question and answered
differently, so the raw one is gone and the baseline is the single answer.

A baseline records debt, so fixing a baselined site means updating the baseline
in the same commit: an entry left behind says a defect is still there when it is
not. `check-sanitize-x.sh` enforces the same thing from the other side, and
refuses the other ways a sanitizer run can report nothing:

- The harness line must name `variant=release-sanitize`. An uninstrumented
  binary writes a log with no report in it, and nothing in that log says so.
- `suppressions=` and `log_path=` are dropped from `ASAN_OPTIONS`,
  `LSAN_OPTIONS` and `UBSAN_OPTIONS` before the harness starts. Either one turns
  a real report into silence, and the gate's job is to see the report.
- A baseline with entries next to a log with no report fails. That is either the
  fixes landing without their baseline update or the sanitizer never running,
  and the log cannot say which.

That verdict lives in `scripts/lib/sanitize-x-report.sh` rather than in the gate,
and `tests/x/test-95-sanitize-x-report.sh` feeds it recorded output, so each
refusal is pinned by a test that needs no X server.

## The gates

| Gate | Command | What it proves |
|------|---------|----------------|
| build | `make -j$(nproc)` | the tree compiles |
| flake | `scripts/check-flake.sh` | the dev shell in `flake.nix` evaluates for this system, so a syntax error, a moved attribute or a package that no longer exists fails here and not at the next `direnv allow` |
| unit | `make test` | the unit suite across hub, state machines, targets, components |
| paths | `scripts/check-paths.sh` | no committed file names a home directory or a personal source tree |
| test-src | `scripts/check-test-src.sh` | every `test-*.c` with a `TEST_GROUP` is in `TEST_SRC`, so its assertions actually run |
| bead refs | `scripts/check-bead-refs.sh` | no source, script, build file or CI config cites a beads id; the reason goes in the code |
| docs | `scripts/check-docs.sh` | documents name symbols and beads ids; no line references, no counts a command already prints, no completion percentage |
| format | `scripts/format-check.sh` | every tracked C file and header matches `.clang-format`; `--fix` rewrites the same list (`make format`) |
| async | `scripts/check-async.sh` | no new blocking call in the event loop |
| commit-msg | `scripts/check-commit-msg.sh` | no `Co-Authored-By`, no "generated with", no robot footer |
| sanitize | `scripts/check-sanitize.sh` | no *new* leak site under ASan; known ones come from `tests/quality/asan-baseline.txt` |
| debug build | `make clean && make DEBUG=1 && make test` | the `-pedantic` build, which is what caught the zero-length array in `tiling.c` |
| build variants | `scripts/check-build-variants.sh` | `make`, `make SANITIZE=1` and `make DEBUG=1` build in one tree without a clean, and each leaves its own binary in `./wm` |
<<<<<<< HEAD
| harness variant | `scripts/check-harness-variant.sh` | `scripts/xephyr-test.sh`, `tests/x/run.sh` and `scripts/perf-test.sh` build the variant named by `DEBUG` and `SANITIZE`, print the variant, the binary and the log before they start, and do not replace `./wm` |
| seam | `scripts/check-seam.sh` (`make test-x`) | X behaviour: mapped, tiled, focused, tags, EWMH. Known failures come from `tests/quality/seam-baseline.txt`; a new one fails the run. Runs before every push; starts Xvfb when there is no display, and fails rather than skipping when neither is available |
| sanitize | `make test SANITIZE=1` | memory and UB errors; no leak allowed |
||||||| parent of e4bad (drop the CI sanitize job, which ignored the leak baseline)
| harness variant | `scripts/check-harness-variant.sh` | `scripts/xephyr-test.sh` and `tests/x/run.sh` build the variant named by `DEBUG` and `SANITIZE`, print the variant, the binary and the log before they start, and do not replace `./wm` |
| seam | `make test-x` | X behaviour: mapped, tiled, focused, tags, EWMH. Known failures come from `tests/quality/seam-baseline.txt`; a new one fails the run |
| sanitize | `make test SANITIZE=1` | memory and UB errors; no leak allowed |
=======
| harness variant | `scripts/check-harness-variant.sh` | `scripts/xephyr-test.sh` and `tests/x/run.sh` build the variant named by `DEBUG` and `SANITIZE`, print the variant, the binary and the log before they start, and do not replace `./wm` |
| seam | `make test-x` | X behaviour: mapped, tiled, focused, tags, EWMH. Known failures come from `tests/quality/seam-baseline.txt`; a new one fails the run |
>>>>>>> e4bad (drop the CI sanitize job, which ignored the leak baseline)
| sanitize-x | `scripts/check-sanitize-x.sh` | the X path under the sanitizers: the run reports `variant=release-sanitize`, no ASan error, and no leak site or UBSan report missing from `tests/quality/sanitize-x-baseline.txt`. `detect_leaks` and `leak_check_at_exit` are forced on, and `suppressions=` and `log_path=` are dropped, in `ASAN_OPTIONS` and `LSAN_OPTIONS` |

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
3. A hook gate needs no workflow edit. The `gates` job runs `lefthook run
   pre-push --force`, so the command from step 2 runs in CI. Pasting that
   command into `.github/workflows/quality.yml` as well is a second
   implementation; the unit-suite sanitizer job was that, and it ignored
   `tests/quality/asan-baseline.txt`. A gate that needs a display cannot be a
   hook. Add a job for it in the workflow, as `seam` and `sanitize-x` are.
4. Add a row to the table above, and to `docs/TESTING.md` if a person would run
   it by hand.

If step 1 is not possible, the gate is not deterministic yet. That is a reason
to leave it out, not to add a heuristic.

## Running everything by hand

```bash
make hooks                  # once per clone; points git at .githooks/
lefthook run pre-push --force   # every gate that runs before a push, changed files or not
lefthook run pre-commit     # just the commit-time gates
LEFTHOOK_EXCLUDE=beads lefthook run pre-push --force   # without the beads export
<<<<<<< HEAD
scripts/check-seam.sh       # the seam suite as the push gate runs it
scripts/check-flake.sh      # the dev shell in flake.nix evaluates
make test-x                 # the same suite, spelled out (needs a display)
make test SANITIZE=1        # the unit suite under the sanitizers
||||||| parent of e4bad (drop the CI sanitize job, which ignored the leak baseline)
make test-x                 # seam tests (needs a display; Xephyr handles that)
make test SANITIZE=1        # the unit suite under the sanitizers
=======
make test-x                 # seam tests (needs a display; Xephyr handles that)
scripts/check-sanitize.sh    # the unit suite under the sanitizers, against the leak baseline
>>>>>>> e4bad (drop the CI sanitize job, which ignored the leak baseline)
scripts/check-sanitize-x.sh # the WM in a nested server under the sanitizers
scripts/check-build-variants.sh   # release, sanitize and debug builds in one tree
scripts/check-harness-variant.sh  # the harnesses run the variant they report
```

## What is deliberately not a gate

- **`scripts/xephyr-test.sh`.** It checks the map and tile path with two or
  five windows, which `make test-x` covers with stricter assertions in
  `tests/x/test-30-tiling-two-windows.sh` and `tests/x/test-80-many-windows.sh`,
  and it has no baseline, so the first known-failing window assertion would turn
  it into a permanently red gate. It stays the acceptance command a person runs
  to watch a session, and `make test-x` is the gate.

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
