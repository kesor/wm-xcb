---
name: wm-5s
description: File harness friction as beads while you work. Use when a command, gate, script, or documented step does not work first time, when you need a workaround to continue, or when you notice something in the way that is not part of the task (stale file, tool not on PATH, contradictory instructions, check that does not run). Also use when asked to triage friction reports.
---

# 5S friction reporting

A command that does not work first time is a defect in the harness, not in
you. Every session that rediscovers it pays the same cost. File it once, keep
working, and the harness gets better.

The report is cheap on purpose: three fields, one bead, then back to the task.

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
bd search "lefthook PATH"            # is it already reported?

bd create --type=chore --priority=1 --parent=wm-dfg --labels=friction \
  --title "friction(seiton): lefthook missing from the worker shell" \
  --description "What I tried: lefthook run pre-commit
What happened: command not found
What worked: nix develop --command lefthook run pre-commit
Cost: one guess, plus reading the hook script to find the fallback.
Suggested fix: start the worker shell inside the dev shell."
```

Title shape: `friction(<bucket>): <the thing>, <the symptom>`.
Description: four lines — tried, happened, worked, cost — plus a suggested fix
if you have one. Paste the exact command; paraphrase loses the detail that makes
it fixable.

## Then keep going

File, then continue with the workaround. Do not stop to fix harness code unless
the fix is in a file your issue already touches; if it is, fix it in the same
commit and link the friction bead in the commit body.

Do not file the same friction twice. Search first (`bd search`), and if the
existing bead is open, add a comment with the new data instead of creating
another.

## Filing this yourself, from a worker

Workers run with an instruction to file friction beads. That instruction is this
skill. Two lines in your final message: the beads you filed, and the
workarounds you used that you did not file.

## Triage

When closing a friction bead, the fix is not complete until one of these is
true:

- a gate in `lefthook.yml` and `.github/workflows/quality.yml` covers it, or
- a line in `AGENTS.md`, `docs/QUALITY.md`, or `docs/TESTING.md` says what to do,
  or
- the bead says explicitly why neither applies.

That is the *shitsuke* step, and it is why the epic exists.
