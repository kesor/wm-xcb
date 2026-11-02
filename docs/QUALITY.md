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
| `pre-commit` | every commit | ~5 s | paths, docs, test-src, bead refs, workflow, beads export, formatting, build, unit tests, async rule |
| `commit-msg` | every commit | instant | message rules (no AI attribution) |
| `pre-push` | every push | ~80 s | the above plus the flake evaluation, the shell lint, the seam suite, the sanitize gate, the `-pedantic` debug build, the standalone tests, the build variants, and the harness variants |
| CI `gates` | every push and PR | ~2 min | `lefthook run pre-push --force` with the beads job excluded |
| CI `seam` | every push and PR | ~1 min | `make test-x` on the private Xvfb the run starts, required: known failures are baselined |
| CI `sanitize-x` | every push and PR | ~1 min | `scripts/check-sanitize-x.sh`: the WM in a nested server under ASan and UBSan |

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

The sanitizer coverage is split the same way the code is. The unit suite runs
with no X connection, and the map, tile and focus requests return before they
are built without one, so the `sanitize` gate inside the gates job cannot see
them: a 2-byte value passed where XCB reads 4 left every gate green and aborted
the WM on the first `MapRequest`. The `sanitize-x` job covers that half by
running a real WM in a nested server under ASan and UBSan. It is not in
`lefthook.yml` because it rebuilds the tree under the sanitizers and takes about
a minute; the seam suite it sits next to is a push gate, and needs no display of
its own because `scripts/check-seam.sh` starts a private Xvfb for every run.

The unit suite under the sanitizers is the `sanitize` gate in the `gates` job,
not a job of its own. A job that ran `make test SANITIZE=1` raw was red from the
first push: the suite leaks at allocation sites that
`tests/quality/asan-baseline.txt` records, and only `scripts/check-sanitize.sh`
reads that baseline. Two places asked the same question and answered
differently, so the raw one is gone and the baseline is the single answer.

A baseline records debt, so fixing a baselined site means updating the baseline
in the same commit: an entry left behind says a defect is still there when it is
not. Both sanitizer gates enforce that from the other side, and refuse the other
ways a sanitizer run can report nothing:

- The run must be one the sanitizer produced. `check-sanitize-x.sh` reads the
  `variant=release-sanitize` line its harness prints, and `check-sanitize.sh`
  checks the test binary it just ran for the ASan runtime: an uninstrumented
  build writes a log with no report in it, and nothing in that log says so.
- `suppressions=` and `log_path=` are dropped from `ASAN_OPTIONS`,
  `LSAN_OPTIONS` and `UBSAN_OPTIONS` before the run starts. Either one turns a
  real report into silence, and the gate's job is to see the report.
- A baseline with entries next to a log with no report fails. That is either the
  fixes landing without their baseline update or the sanitizer never running,
  and the log cannot say which.

That verdict lives in `scripts/lib/sanitize-report.sh`, which both gates read and
which `tests/x/test-96-sanitize-report.sh` feeds recorded output, so each refusal
is pinned by a test that needs no X server. `scripts/lib/sanitize-x-report.sh`
keeps only what the nested-server gate has on its own: the harness line.

## The gates

| Gate | Command | What it proves |
|------|---------|----------------|
| build | `make -j$(nproc)` | the tree compiles |
| flake | `scripts/check-flake.sh` | the dev shell in `flake.nix` evaluates for this system, so a syntax error, a moved attribute or a package that no longer exists fails here and not at the next `direnv allow` |
| unit | `make test` | the unit suite across hub, state machines, targets, components |
| paths | `scripts/check-paths.sh` | no committed file names a home directory or a personal source tree |
| workflow | `scripts/check-workflow.sh` | every file under `.github/workflows/` parses and passes actionlint. It cannot be a CI job: the run that would report a broken workflow is the run the broken workflow removed, so it runs before the push |
| test-src | `scripts/check-test-src.sh` | every `test-*.c` with a `TEST_GROUP` is in `TEST_SRC`, so its assertions actually run |
| bead refs | `scripts/check-bead-refs.sh` | no source, script, build file or CI config cites a beads id; the reason goes in the code |
| docs | `scripts/check-docs.sh` | documents name symbols and beads ids; no line references, no counts a command already prints, no completion percentage |
| format | `scripts/format-check.sh` | every C file and header, tracked or about to be, matches `.clang-format`; `--fix` and `--list` use the same list (`make format`) |
| shellcheck | `scripts/check-shellcheck.sh` | every shell file with a shebang or a `.sh` name, tracked or about to be, has no finding missing from `tests/quality/shellcheck-baseline.txt`. A finding the baseline records with a count does not fail; an extra occurrence of it does |
| async | `scripts/check-async.sh` | no new blocking call in the event loop |
| commit-msg | `scripts/check-commit-msg.sh` | no `Co-Authored-By`, no "generated with", no robot footer |
| debug build | `make clean && make DEBUG=1 && make test` | the `-pedantic` build, which is what caught the zero-length array in `tiling.c` |
| build variants | `scripts/check-build-variants.sh` | `make`, `make SANITIZE=1` and `make DEBUG=1` build in one tree without a clean, and each leaves its own binary in `./wm` |
| harness variant | `scripts/check-harness-variant.sh` | `scripts/xephyr-test.sh`, `tests/x/run.sh` and `scripts/perf-test.sh` build the variant named by `DEBUG` and `SANITIZE`, print the variant, the binary and the log before they start, and do not replace `./wm` |
| seam | `scripts/check-seam.sh` (`make test-x`) | X behaviour: mapped, tiled, focused, tags, EWMH, events already read into libxcb's queue, and idle CPU stays under the budget. Known failures come from `tests/quality/seam-baseline.txt`; a new one fails the run. Runs before every push on a private Xvfb it starts itself (`WM_USE_SESSION_DISPLAY=1` draws on the session instead), and fails rather than skipping when that server cannot start |
| sanitize | `scripts/check-sanitize.sh` | no leak site or UBSan report missing from `tests/quality/asan-baseline.txt`, and the suite that produced the log ran under ASan. `detect_leaks` and `leak_check_at_exit` are forced on, and `suppressions=` and `log_path=` are dropped, in `ASAN_OPTIONS` and `LSAN_OPTIONS` |
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
- **Baseline** — the calls that are known and tracked in beads. The script
  prints how many there are; the remaining ones are all in
  `bd list --parent wm-46k.2`.

A violation that is in neither fails. That is the point: the file only shrinks.
Adding an entry is a deliberate act:

```bash
scripts/check-async.sh --update-baseline
git diff tests/quality/async-baseline.txt    # read it before committing
```

Fixing a violation leaves a stale entry, which the script reports and which
should be removed with the same command. A stale entry is not an error, so
nobody is punished for improving the tree.

## The shellcheck baseline

`scripts/check-shellcheck.sh` fails on a finding that
`tests/quality/shellcheck-baseline.txt` does not record. Every line is
`<file>|<code>|<message>|<occurrences>`. The line and column are left out so an
edit above a finding does not invalidate its entry, and the count is kept so a
second occurrence of a finding that is already known still fails.

The gate refuses two shapes of a run rather than reading them as clean: a log
with a line in it that is not a finding, and a log with no finding at all next
to a baseline with entries. The second is either the fixes landing without their
baseline update or a lint that never ran. `scripts/lib/shellcheck-report.sh`
does that judging, and `tests/x/test-97-shellcheck-report.sh` feeds it recorded
output so each refusal is pinned without a shell to lint.

Findings change between shellcheck
releases, so the version is the one in the dev shell, which `flake.nix` pins with
the rest of it; a baseline generated by another version reports findings that are
not new. And shellcheck resolves a relative `# shellcheck source=` against the
caller's working directory rather than the file's, so the gate passes `-P` with
each file's own directory: without it every script that sources `scripts/lib/`
reports SC1091 and `-x` follows nothing. `-x` follows a sourced file for the
names it defines and does not report that file's findings. `-a` would, once per
script that sources it, so a new caller would move a count the baseline already
recorded. The libraries under `scripts/lib/` have no shebang, so the file list
is every `.sh` plus every other file whose first line is a shell shebang. The
gate also passes `--norc`, so a shellcheckrc outside the repository cannot
change the answer.

Fixing a finding means updating the baseline in the same commit:

```bash
scripts/check-shellcheck.sh --update-baseline
git diff tests/quality/shellcheck-baseline.txt    # read it before committing
```

A stale entry is not an error, so improving the tree is not punished. An entry
left behind says a defect is still there when it is not.

## Adding a gate

1. Write the check as a script under `scripts/` that exits non-zero on failure
   and prints `file:line` or the failing thing. No network, no model.
2. Add it to the right layer in `lefthook.yml` with a `fail_text` that says what
   to do about it.
3. A hook gate needs no workflow edit. The `gates` job runs `lefthook run
   pre-push --force`, so the command from step 2 runs in CI. Pasting that
   command into `.github/workflows/quality.yml` as well is a second
   implementation; the unit-suite sanitizer job was that, and it ignored
   `tests/quality/asan-baseline.txt`. The seam and sanitize-x jobs are not that
   kind of copy: seam uploads the logs the hook run does not keep, and
   sanitize-x is not a hook because it rebuilds under the sanitizers. Neither
   needs a display of its own; the harness starts a private Xvfb.
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
scripts/check-seam.sh       # the seam suite as the push gate runs it
scripts/check-flake.sh      # the dev shell in flake.nix evaluates
scripts/check-shellcheck.sh # the shell lint, against its baseline
make test-x                 # the same suite, spelled out; starts a private Xvfb
scripts/check-sanitize.sh   # the unit suite under the sanitizers, against the leak baseline
scripts/check-sanitize-x.sh # the WM in a nested server under the sanitizers
scripts/check-workflow.sh   # every .github/workflows/ file parses
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
