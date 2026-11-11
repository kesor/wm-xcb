---
name: wm-5s
description: File harness friction as beads while you work. Use when a command, gate, script, or documented step does not work first time, when you need a workaround to continue, or when you notice something in the way that is not part of the task (stale file, tool not on PATH, contradictory instructions, check that does not run). Also use when asked to triage friction reports.
---

# 5S friction reporting

A command that does not work first time is a defect in the harness, not in
you. Every session that rediscovers it pays the same cost. File it once, keep
working, and the harness gets better.

The report is cheap on purpose: three fields, one bead, then back to the task.
What is worth optimizing at all, and which borrowed techniques do not hold in
this environment, is `.agents/skills/wm-line/SKILL.md`. A report whose fix would
be an optimization the four rules refuse does not get filed as friction; it gets
argued against once, in the bead's comments.

These five words are housekeeping's: sort, set in order, shine, standardise,
sustain. This project uses them to file friction against a loop, not to tidy a
workplace. The difference is not pedantry — an Imai 5S audit looks at shelves and
floor markings; this one looks at commands, gates and documents that cost a
session its time. Do not report a tidy desk under this skill, and do not let a
real housekeeping problem hide inside it either.

## When to file

File a bead when any of these happens:

- a command in `docs/`, `AGENTS.md`, a skill, or a commit message fails or
  behaves differently than described
- you need a workaround to continue (a flag, a wrapper, an extra `cd`, running
  something through `nix develop` by hand)
- two instructions contradict each other
- a check passes or fails for the wrong reason, or a check you would expect
  does not exist
- something is in the way and does not belong: stale file, dead code, misleading
  name, tool missing from the dev shell

Do **not** file: defects in the window manager itself (those are normal beads),
and things the issue you are working on already covers.

One more check before you file, and it is about who the finding serves. A finding
about the tracker, the briefs, the recipes or the gates is **internal demand**:
its customer is the loop, not the product. That is real work, and on 2026-11-10
it was 132 of the 165 open beads - which is why two and a half days of it produced
no commits to `src/`. So before filing one, ask what it unblocks: if the answer is
another bead about filing beads, it is a comment on the oldest bead in its class,
not a new id. `python3 worker.py recurrence` shows whether it repeats something
already fixed; `python3 worker.py metrics` shows the run's failure-demand share,
which is how much of the queue arrived because an earlier attempt did not land.

## Which 5S bucket

The Japanese terms are the vocabulary; the practical rule is the last column.
Filing the bucket is what makes the report actionable.

| Bucket | Meaning here | Typical report | Priority |
|--------|--------------|----------------|----------|
| **seiri** 整理 | sort: remove what does not belong | stale file, dead code, unused script, duplicated list, log noise on every commit | P1 if it blocks or costs every run, else P2 |
| **seiton** 整頓 | set in order: put it where it is found | tool missing from the dev shell, wrong Makefile target, a script that only runs from one directory, a path that only exists on the author's machine | **P1** |
| **seisō** 清掃 | shine: clean the surface | formatting drift, compiler warnings, lint noise, a leak reported by a sanitizer | P2 |
| **seiketsu** 清潔 | standardise: write the rule down | undocumented convention, contradictory instructions, a rule that lives only in someone's head | **P1** |
| **shitsuke** 躾 | sustain: make it stick | a fixed problem with no gate, a gate that can be skipped silently, a check that does not run in CI | **P1** |

A standard here is the current best known way to do the job, and the people who
do the work are the ones who revise it — a rule written by someone who never ran
the command is a wish with a heading. Sustaining it is the work, not the
ceremony: a document nobody re-reads has already decayed.

Two rules override the table: anything that makes work stop gets **P0**, and a
`shitsuke` report (add the gate) is always worth **P1**, because it is the one
that stops the recurrence.

The bucket says how to fix it. What it cost is a muda, a different axis, and
naming both in the title is what makes three reports of the same thing visible
as three: `friction(seiton, waiting): ...`. `.agents/skills/wm-3m/SKILL.md` has
the categories and how to judge a run.

## How to file

Parent epic: `wm-dfg` ("Harness friction"). Verify with
`bd list --type=epic | grep friction` if the id is not found.

```bash
# is it already reported? `bd search` reads titles and ids only, one term per
# run, and stops at 50 results unless told --limit 0; the description needs
# --desc-contains. Neither takes --status: --status=open drops in_progress.
bd search lefthook --limit 0
bd list --limit 0 --desc-contains lefthook

bd create --type=chore --priority=1 --parent=wm-dfg --labels=friction \
  --no-inherit-labels \
  --title "friction(seiton): lefthook missing from the worker shell" \
  --acceptance "lefthook run pre-commit passes in the worker shell, checked by a command listed in docs/QUALITY.md" \
  --description "What I tried: lefthook run pre-commit
What happened: command not found
What worked: nix develop --command lefthook run pre-commit
Cost: one guess, plus reading the hook script to find the fallback.
Suggested fix: start the worker shell inside the dev shell."
```

`--no-inherit-labels` is not optional: `bd` copies the parent's labels onto the
child, and `wm-dfg` carries `harness`, so a bead created without it is born
labelled as a harness fix and the loop skips it without reading it. Add
`--labels=friction,harness` yourself, and only when the fix is in the worker
harness repository rather than this tree.

`--acceptance` is not optional either. `scripts/check-bead-acceptance.sh` fails
the next commit on any live bead with no acceptance criteria that
`tests/quality/bead-acceptance-missing-baseline.txt` does not record as debt.
The create is the only write a worker may make to a bead's fields, so a bead
filed without this flag is debt from the moment it exists, and the two ways out
it prints are the flag you skipped and adding it to the baseline. Write what a
command will show when the finding is fixed.

Title shape: `friction(<bucket>): <the thing>, <the symptom>`.
Description: four lines — tried, happened, worked, cost — plus a suggested fix
if you have one. Paste the exact command; paraphrase loses the detail that makes
it fixable.

## Then keep going

File, then continue with the workaround. Do not stop to fix harness code unless
the fix is in a file your issue already touches; if it is, fix it in the same
commit and link the friction bead in the commit body.

Do not file the same friction twice. Search first, and not with `bd search`: it
matches titles only, so it misses a bead whose symptom is in its description.
Grep the open list for each distinctive term of the finding -
`bd search "<term>" --limit 0` for titles and ids, `bd list --limit 0
--desc-contains "<term>"` for descriptions, one term per run - and read the
open `wm-dfg` beads. Neither carries `--status`: `--status=open` is the stored
status open and drops an in_progress bead, which is where the bead you would
duplicate usually sits. If an existing bead is open, add a comment with the new
data instead of creating another.

## Filing this yourself, from a worker

Workers run with an instruction to file friction beads. That instruction is this
skill. Two lines in your final message: the beads you filed, and the
workarounds you used that you did not file.

## Triage

When closing a friction bead, the test that chooses is `DESIGN.md` section 10a,
in the harness directory: first, can a command catch it; second, if not, would a
general statement have caught the class? The answers give the bead one of three
homes:

- **a deterministic check**: a gate covers it, in `lefthook.yml` and
  `.github/workflows/quality.yml`, or
- **a general rule** says what to do, a statement of the class, in `AGENTS.md`,
  a skill, `docs/QUALITY.md`, or `docs/TESTING.md`, or
- **nothing**: no command can catch it and no general statement would have caught
  the class. Say in the bead that both questions failed, add no gate and
  no rule, and close it. A rule written here anyway would name one incident, and
  the next reader would still have to decide whether it applied to them.

When the answer is a device, take the strongest that fits, in order: make the
wrong action impossible, then make it fail at the moment it is taken, then write
the instruction down. A hook is a warning, because `--no-verify` exists; a check
that runs in CI on a protected branch is a control. A device nobody has seen fail
is an assumption, so prove a new one by removing the thing it must catch.
`DESIGN.md` section 10 has the three levels.

Two more rules for the triage, both learned the hard way:

- **Prefer the fix that removes work over the fix that adds capacity.** Deleting
  a step costs nothing; a retry, a longer timeout, or one more parallel chain
  costs every future run. If the bead can be closed by removing the thing that
  failed rather than by making failure cheaper, remove it.
- **A fix outside this project's sphere of control is not a fix, it is a
  handover.** Another team's pipeline, a vendor's tool, a service nobody here
  operates: say so in the bead and name who owns it. Otherwise the report sits
  open, and the next session reads it as work it can pick up.

That is the *shitsuke* step, and it is why the epic exists.
