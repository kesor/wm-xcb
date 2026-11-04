# Quality Gates

*Written 2026-11-01. Every gate below is a command. None of them consults a
model.*

> **Bottom line:** `lefthook.yml` is the list of gates and
> `.github/workflows/quality.yml` runs the same commands in CI. A gate is added
> in one place and called from both. If a check cannot be reduced to a command
> that gives the same answer twice, it does not belong here.
>
> CI calls `lefthook run pre-push --force --no-auto-install`. Without `--force`
> lefthook computes the files a push touched, and a CI checkout has no push
> range: the branch tip is the commit already checked out, the file list is
> empty, and lefthook skips every command and reports success. The claim that CI runs the same commands
> was false until the flag was there. The job's runtime is the check: tens of
> seconds means the gates ran.

---

## Layers

`scripts/gate-list.sh` prints which gates each hook runs, read out of
`lefthook.yml`. Ask it instead of trusting a copy: this table used to list the
gates by hand, and it drifted twice - a header comment claiming a pre-push set
missing four of the gates, and a skill table still saying `make test` a commit
after the hook switched to `scripts/check-unit.sh`. A copy in prose is a claim
nothing checks.

```bash
scripts/gate-list.sh            # every hook and its gates
scripts/gate-list.sh pre-push   # one hook
lefthook run pre-push --force   # run them all by hand
```

Without `--force` a hook runs only the gates its globs match, and a hand run has
no staged or pushed files to match, so everything skips and the summary is green.
`--force` is what the merge and CI use, and it is what to use by hand.

| Layer | When | Runs |
|-------|------|------|
| `pre-commit` | every commit | `scripts/gate-list.sh pre-commit` |
| `commit-msg` | every commit | `scripts/gate-list.sh commit-msg` |
| `pre-push` | every push | `scripts/gate-list.sh pre-push` |
| CI `gates` | every push and PR | `lefthook run pre-push --force --no-auto-install` with the beads job excluded |
| CI `seam` | every push and PR | `scripts/check-seam.sh`: known failures are baselined, a new one fails |
| CI `sanitize-x` | every push and PR | `scripts/check-sanitize-x.sh`: the WM in a nested server under ASan and UBSan |

Runtimes are not in this table. The hook prints its own summary with a duration
per gate, and a number written here is stale the next time a gate gets slower.

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
not a job of its own. A job that ran `make test SANITIZE=1` raw reported every
leak site, including the ones `tests/quality/asan-baseline.txt` deferred, and
only `scripts/check-sanitize.sh` reads that baseline. Two places asked the same
question and answered differently, so the raw one is gone and the gate is the
single answer. The baseline holds no site now: the suite allocates nothing it
leaves behind, so any report the gate sees is new and fails the push.

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
| unit | `scripts/check-unit.sh` | the unit suite across hub, state machines, targets, components. The gate captures the run and prints its log only when it fails: a passing suite is a single summary line and lefthook prints the verdict and the duration as its own summary, so attaching the log to every commit spends the reader's attention on a step that passed. A run whose summary line is missing, or whose summary counts a failure, fails the gate whatever make exited with. `DEBUG` and `SANITIZE` select the variant, and the pass line names the one make built, so the debug gate's suite cannot pass as release |
| coverage | `scripts/check-coverage.sh` | no production source file is below its line floor in `tests/quality/coverage-baseline.txt`, and no production source file is missing one. Files under `tests/` are printed and carry no floor: a helper's error return is only reachable by breaking the environment |
| paths | `scripts/check-paths.sh` | no committed file names a home directory or a personal source tree |
| workflow | `scripts/check-workflow.sh` | every file under `.github/workflows/` parses and passes actionlint. It cannot be a CI job: the run that would report a broken workflow is the run the broken workflow removed, so it runs before the push |
| test-src | `scripts/check-test-src.sh` | every `test-*.c` with a `TEST_GROUP` is in `TEST_SRC`, so its assertions actually run |
| fakes | `scripts/check-fakes.sh` | every stand-in in `tests/fakes/` names, on its second line, the program it models, cites a line of that program on its third, is named after that program, and names one that is installed on `PATH` outside the fake directory. The check resolves that name to a path, requires a regular file, and looks for the cited line in it. A fake answers every assertion a test asks it, so the program it names is the one part of a test the suite cannot falsify |
| bead refs | `scripts/check-bead-refs.sh` | no source, script, build file or CI config cites a beads id; the reason goes in the code |
| docs | `scripts/check-docs.sh` | documents name symbols and beads ids; no line references, no counts a command already prints, no completion percentage |
| format | `scripts/format-check.sh` | every C file and header, tracked or about to be, matches `.clang-format`; `--fix` and `--list` use the same list (`make format`) |
| shellcheck | `scripts/check-shellcheck.sh` | every shell file with a shebang or a `.sh` name, tracked or about to be, has no finding missing from `tests/quality/shellcheck-baseline.txt`. A finding the baseline records with a count does not fail; an extra occurrence of it does |
| async | `scripts/check-async.sh` | no new blocking call in the event loop |
| commit-msg | `scripts/check-commit-msg.sh` | no `Co-Authored-By`, no "generated with", no robot footer |
| debug build | `scripts/check-debug-build.sh` | the `-pedantic` build, which is what caught the zero-length array in `tiling.c`, and its suite: a pass prints one line, a failure prints the whole log, because 13 standing warnings on every push buried the one that mattered |
| standalone | `scripts/check-standalone.sh` | the link-and-run targets with no X connection, one line on a pass instead of 215 |
| build variants | `scripts/check-build-variants.sh` | `make`, `make SANITIZE=1` and `make DEBUG=1` build in one tree without a clean, and each leaves its own binary in `./wm` |
| harness variant | `scripts/check-harness-variant.sh` | `scripts/xephyr-test.sh`, `tests/x/run.sh`, `tests/x/probe.sh` and `scripts/perf-test.sh` build the variant named by `DEBUG` and `SANITIZE`, print the variant, the binary and the log before they start, and do not replace `./wm` |
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
  it. For code that cannot block: startup paths before the loop runs. The reason
  is required on that same line; an `allow-blocking:` with nothing after it is
  printed as a waiver without a reason and fails like a violation. A waiver is
  also refused when an XCB handler can reach the function it sits in: the reason
  is a claim about when the code runs, and a handler runs inside the loop. The
  refusal is printed as `WAIVER REFUSED` with the function's name, and the call
  is judged as a violation.
- **Baseline** — the calls that are known and tracked in beads. The script
  prints how many there are, and the file is empty today: the async audit that
  filled it is done and the waits that remain are startup paths with a waiver.
- **Not a violation** — `xcb_poll_for_reply()`, `xcb_poll_for_reply64()` and
  `xcb_discard_reply()`. Their names end in `_reply`, and libxcb documents all
  three as not waiting: the first two return 0 while the reply has not arrived.
  Flagging them would tell the author to waiver the call the rule asks for.
- **Not read at all** — the text of a comment or a string literal. The rules run
  over a copy of each file from `async_strip_comments()` in
  `scripts/lib/async-scan.sh`, with comments and string contents blanked and the
  file's line structure kept. A call name in prose is not a call, whichever line
  of a `/* ... */` block it starts.

`tests/x/test-101-async-rules.sh` runs the gate over probe files. Each blocking
call in the probe must be reported once, and a comment, a string,
`xcb_poll_for_reply()`, `xcb_poll_for_reply64()`, `xcb_discard_reply()` and
`waitpid()` with `WNOHANG` must not. A reasoned `allow-blocking:` is counted
as waived. The probe names those calls; it does not read the rule array, so a
rule added later is unchecked until the probe gains a line for it. An
`allow-blocking:` with no reason, and a waiver the event loop can reach, are
pinned by `tests/x/test-100-async-scan.sh`.

The rules also cover blocking names that do not end in `_reply(`: the
`xcb_*_replies()` family waits once per atom or screen, `xcb_aux_sync()` is a
round trip, and `xcb_key_symbols_get_keycode()` and
`xcb_key_symbols_get_keysym()` wait for the keyboard mapping.
`xcb_key_symbols_alloc()` only sends that request, so it is not a rule.
Each blocking name is waived where it runs before the loop and nowhere else.

What "where it runs" means is read off the sources, in
`scripts/lib/async-scan.sh`: the handlers named in an `xcb_handler_register()`
call, plus everything those reach through a call written in the tree. A name
mentioned in a comment inside another function body counts as a call from it,
so the analysis errs toward refusing a waiver. A callback stored in a struct
field and called through that field is not followed, because the source does
not say when it runs; `xcb_handler_register()` is how the tree hands a function
to the loop, and it is the one that is followed.
`tests/x/test-100-async-scan.sh` pins the decision on small sources.

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
   to do about it, then prove it fires: `lefthook run pre-commit --command
   <name> --force --no-auto-install`. Without `--force`, a command with a `glob`
   is skipped when no staged file matches, and lefthook answers the skipped job
   with `no job matching <name> found`, which reads as a gate that is not wired
   at all.
3. A hook gate needs no workflow edit. The `gates` job runs `lefthook run
   pre-push --force --no-auto-install`, so the command from step 2 runs in CI.
   Pasting that command into `.github/workflows/quality.yml` as well is a second
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
lefthook run pre-push --force --no-auto-install   # every gate that runs before a push, changed files or not
lefthook run pre-commit --no-auto-install     # just the commit-time gates
LEFTHOOK_EXCLUDE=beads lefthook run pre-push --force --no-auto-install   # without the beads export
scripts/check-seam.sh       # the seam suite as the push gate runs it
scripts/check-flake.sh      # the dev shell in flake.nix evaluates
scripts/check-shellcheck.sh # the shell lint, against its baseline
make test-x                 # the same suite, spelled out; starts a private Xvfb
scripts/check-sanitize.sh   # the unit suite under the sanitizers, against the leak baseline
scripts/check-sanitize-x.sh # the WM in a nested server under the sanitizers
scripts/check-unit.sh       # the unit suite as the commit gate runs it: one line on a pass, the log on a failure
DEBUG=1 scripts/check-unit.sh   # the same suite in the debug build
scripts/check-workflow.sh   # every .github/workflows/ file parses
scripts/check-build-variants.sh   # release, sanitize and debug builds in one tree
scripts/check-harness-variant.sh  # the harnesses run the variant they report
scripts/check-fakes.sh      # every stand-in names an installed program and cites a line of it
```

Pass `--no-auto-install` to a by-hand `lefthook run`: the hook wrapper passes it,
and without it lefthook treats the committed `core.hooksPath` as a mistake and
prints a block offering to reset it, which would undo `make hooks`.

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

## Coverage and fuzzing

These three run in the hook now, and by hand, and in CI. They used to run only in
CI, and that is where they cost the most: a worker and a reviewer both saw green
on a tree that CI then rejected, twice in one night, each time losing a cycle.
Shifted left, the person who wrote the code is the one who sees the number.

```bash
scripts/check-complexity.sh            # per-function cognitive complexity (0.3s)
scripts/check-coverage.sh              # line and branch coverage of the unit suite (7s warm)
scripts/check-coverage.sh --show-zero  # also list what the suite never executes
scripts/fuzz.sh 5                      # the fuzzers, short: the hook's version
scripts/fuzz.sh 60                     # the same, long: by hand or in CI
```

In the hook: complexity is a `pre-commit` gate on the changed files, and
complexity, coverage and a 5-second fuzz run are `pre-push` gates. `scripts/fuzz.sh`
with no argument runs 20 seconds per harness.

**Complexity.** `scripts/check-complexity.sh` runs clang-tidy's
cognitive-complexity check over every source file and compares the number with
`tests/quality/complexity-baseline.txt`, which lists `<function> <number>` for
everything at 11 or above. A function above its number fails, and so does one at
or above the floor with no entry: a new god-function arrives with somebody
deciding the number is acceptable. It writes its own compile database from
`compile_flags.txt` rather than calling `make compile-commands`, which does a
full clean build.

In CI the job runs it with `--advisory`, and the reason is in wm-9n0.30: the same
commit and the same clang-tidy reported `mod_key_name` at 14 on the author's
machine and 38 on a runner, so the gate cannot fail a merge on the difference
until that is explained. The hook gates still fail, on the machine where the
baseline was measured.

**Coverage.** `scripts/check-coverage.sh` builds the unit suite with gcov
instrumentation, runs it, and prints line and branch coverage per file. A file
below its recorded floor in `tests/quality/coverage-baseline.txt` fails, and so
does a source file with no floor at all: a new file arrives with somebody
deciding what covers it. `--update-baseline` rewrites the floors from the
current run; doing that to turn a red run green is the same as deleting a test,
so say why in the commit. The floor written is the measured value less 1.5
points, because the launcher tests fork and exec and their coverage moves by
about a point between runs of the same tree.

The floors cover the production sources only. A file under `tests/` is printed
in the table and carries no floor, in the baseline or in the comparison: its
missed lines are the guards inside the tests' own helpers (a `mkdtemp`, `strdup`
or `fopen` that failed, a stand-in file the test could not write), and no
passing test reaches one without breaking its environment. A floor there
measures how much defensive code a helper carries, and it fails the branch that
adds tests: adding one helper that removes a directory dropped
`tests/unit/test-launcher.c` below a floor set when the file was a third of its
size, and that failed a merge whose unit suite passed.

A file at 0% is the useful number. On the tree this was written against, three
source files were never executed by any unit test: `wm-states.c`,
`wm-xcb-events.c` (the handlers nobody registers) and
`src/components/connection-sm.c` (the state machine with no instance). A branch
percentage in single digits on the monitor manager is the same signal: the code
is there, and nothing proves it runs.

**Fuzzing.** `scripts/fuzz.sh` builds `tests/fuzz/fuzz-*.c` with clang, ASan and
UBSan, and runs each for the seconds given. The two harnesses feed the X event
dispatcher and the window-rule matcher. Both take their input from a place a
client controls: a client sends the event fields and it chooses `WM_CLASS` and
`WM_NAME`. A crash prints its input, and dropping that file into
`tests/fuzz/corpus/<target>/` makes it a case every later run re-tests.

## Globs are a list, not an alternation

`glob: "*.{c,h}|Makefile"` matches nothing in lefthook 2.1: the string is read as
one pattern, not a set. Write it as a YAML list:

```yaml
glob: ["*.{c,h}", "Makefile"]
```

The first version of the skip-what-cannot-matter globs used `|`, so the build,
unit, seam, coverage and fuzz gates skipped themselves on every commit and push
for an afternoon while their summaries read green. `--force` ignores globs, which
is why the merge and CI never noticed - and why the only thing that caught it was
a retro reading the gate output.
