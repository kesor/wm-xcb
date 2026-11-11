---
name: wm-3m
description: Judge a worker run and find what to improve, using the three Ms — muda (what the flow lost), mura (how uneven it was), muri (how overburdened it was) — then close the judgement with one dated target condition. Use after a run finishes, when asked how a run went, when deciding whether a friction report is worth filing, or when comparing two runs to see whether a change helped.
---

# 3M: judging a run

Load this after a run's transcript exists and before deciding what to change. It
is the reading half of the loop; `.agents/skills/wm-5s/SKILL.md` is the writing
half, and `.agents/skills/wm-line/SKILL.md` sits upstream of both: it decides what
to optimize and refuses the optimizations that do not hold here. 5S files a report
by the kind of tidying it needs. 3M says what the run
lost, which is a different axis: a report can be a correct seiri that removes
waiting, and naming both is what lets a session spot the third instance of the
same thing.

The three Ms come from the same place as 5S, and they are not three names for
the same problem:

- **muda 無駄** — work that consumes effort and produces nothing. Category, not
  size: one wasted call and forty wasted calls are the same defect.
- **mura 斑** — unevenness between runs, which is what makes any single
  measurement meaningless and any comparison unfair.
- **muri 無理** — an unreasonable load on one actor, which produces muda
  elsewhere: a reviewer at the end of its context writes a shallow verdict, a
  worker with five beads rushes the fifth.

---

## Muda: the eight categories, as they appear here

Each row names what it looks like in a worker run and one witness from this
project's transcripts. Add a witness when you find a new one; delete the row
when the class stops happening.

| Category | In a worker run | Witnessed |
|---|---|---|
| **Transport** 運搬 | moving work to where it can be done instead of doing it there | a worker copying `build/debug/wm` to `/tmp` to run it; transcripts and logs spread over `$XDG_RUNTIME_DIR/wm-worker-logs`, `archive/`, `sessions/` and `wm-worker-prompts/` |
| **Inventory** 在庫 | work in process that nobody consumes | 80 commits that had never been pushed; 15 open friction beads; worktrees stranded when a run died |
| **Motion** 動作 | the worker moving without progress | a reviewer guessing `src/hub`, `src/*.c`, `src/main.c`; `cat` of a whole file to find one function |
| **Waiting** 手待ち | blocked time, waiting for a person, a model, a check | 40 calls on "why is my DEBUG log empty"; `publish` waiting for checks that did not exist yet; blocked beads |
| **Overproduction** 作りすぎ | output nobody asked for | an unfilled `CLAUDE.md`; a second managed beads block in `.cursor/rules/`; a second code map inside `AGENTS.md`; a long guide nobody pruned |
| **Over-processing** 過剰加工 | more effort than the value needs | an 18-minute review of five beads; re-running gates by hand after a merge that already ran them |
| **Defects** 不良 | rework caused by getting it wrong first | three false claims in one `AGENTS.md` rewrite; a test that made `make SANITIZE=1 test-x` impossible; a CI gates job green for months with every command skipped |
| **Unused talent** 人の活用不足 | the actor was not asked, or not given what it needed | workers not told they may file beads; not given the harness to read; not given `xwininfo`, so one wrote a C program instead |

Ohno identified seven of those. The eighth is contested: Womack and Jones name
"goods and services which do not meet the needs of the customer", and this
project chose unused talent instead. In a loop that builds a tool for its own
operator, unused talent is the waste that recurs — a worker not told it may file
friction, a reviewer not given the log. In a product loop the other eighth is the
bigger one. Do not add a ninth without a witness; two candidates for the last row
is already one more than the list can settle.

Two rules that keep the list honest:

- **A category is a claim about cost, so say what it cost.** "Over-processing:
  the review took 18 minutes" is not a finding. "Over-processing: 18 minutes to
  review 7 commits, of which 2 were the reviewer's own fixes" is.
- **Weigh the finding against what the run produced.** The 18-minute
  `wm-dfg.13` review found that `bd show` does print comments and that the
  `test-src` gate never fired — two false claims that had been copied into three
  documents. By length it was the most expensive review of the day; by value it
  was the cheapest way to find those two. Judge the pair, not the length.

---

## Mura: unevenness

Measure the same things every run and compare them, because a number without a
distribution is a story:

- calls and tool seconds per worker, wall time per worker and per review;
- beads per worker (1 to 5 in this project) and commits per bead;
- gate time (`pre-push` went from ~5 s to ~65 s once the seam suite and the
  flake evaluation were added — that is mura between two eras, not a regression);
- review depth (a verdict that checked everything on a 3-commit branch, and one
  that checked nothing on a 7-commit branch).

Unevenness is not automatically bad: a batch of five small doc fixes *should*
take less than one memory-safety fix. What is bad is not knowing which is which,
because then a regression hides inside a variation.

Two traps when comparing runs. **Regression to the mean**: an extreme run is
followed by a less extreme one with no change at all, so "it improved after our
fix" is the weakest claim available. **Tampering**: reacting to variation the
process always has, which makes the variation worse rather than better (Deming's
funnel). Both are why a finding needs a witness — a second instance, or a gate
that fails — and not a before-and-after of one run. `.agents/skills/wm-line/SKILL.md`
has the four rules behind this and `DESIGN.md` §12 the terms.

---

## Muri: overburden

Look for the load on a single actor:

- context used by a reviewer before it writes its verdict (74% of 500k observed
  on a five-bead review — it went on to approve, which is a reason to check
  what it did *not* read, see "Did not check" in its comment);
- beads per worker: each is a separate commit and a separate set of gates;
- gates per push: every gate is a minute of a person's life per push;
- waits with a timeout (`WORKER_REVIEW_TIMEOUT`, `WORKER_MAX_ROUNDS`): a timeout
  that fires is a muri signal, not a success.

---

## Failure demand: the work that exists because the last one did not land

The three Ms come from the factory, and a factory measures waste differently
from a service: a part number repeats, so rework is visible per unit and a
consumed container is refilled with an identical one. `DESIGN.md` section 9 says
this loop has no part number - "the thousandth is as new as the first" - so every
measure of waste that depends on repetition degrades here. Muda counts effort
spent at a station. Goldratt counts what queues in front of a constraint. Neither
asks **why the item arrived at all**.

John Seddon's distinction is the one that answers it. **Demand** is what the
customer asked for. **Failure demand** is demand the customer did not want to
make: the call asking why the thing has not arrived, the second visit because the
first one was wrong, the bead filed because the last fix was narrower than the
defect. In service systems it is commonly a large share of the total, and it is
the one number that tells you the system's design is producing your workload.

It fits here for a reason no factory measure can match: it attributes an item by
**provenance**, not by content. This cycle exists because that bead was rejected.
This bead exists because that one was closed prematurely. That is a join between
two rows the tracker already holds - `created_at` after `closed_at` on the same
subject, a resume after a verdict, a re-dispatch after a park - so it survives
the absence of part numbers.

Measure two things and put them on the `demand:` line:

- **the run's share**: `python3 worker.py metrics --since 48h` prints the number
  of cycles beyond a bead's first, the share of all cycles they are, and which
  ending each first attempt had. In the run that ended 2026-11-10 it was 60 of
  139 cycles, 43%: 46 after a review sent the work back, 7 after a publish could
  not prove itself, 4 after a timeout, 2 after a refused landing, 1 after a
  failed cycle.
- **the queue's share**: `python3 worker.py recurrence` prints how many open
  friction beads repeat a bead closed before they were created. On 2026-11-10 it
  was 12 of 53.

Report it; do not target it directly. The cheapest way to lower failure demand is
to park every item that fails once, and the number collapses while the customer
gets nothing - the same argument as Goodhart's, applied to the one measure that
looks most like a score. What you target is the **cap** that removes the repeat:
two reviews that send it back and a person reads the verdicts, two publishes that
cannot prove themselves and a person reads the merge, a bead too big to finish in
a turn split before it starts. In the run above those three were 26 of the 60
failure cycles, so the caps are worth more than any sentence written here.

One more distinction worth keeping: **internal demand** is demand whose customer
is the loop itself - a bead about the tracker, the briefs, the gates. On
2026-11-10 it was 132 of 165 open beads, and that ratio is why two and a half days
produced no `src/` commits. It is not waste in the muda sense: fixing the loop is
real work. It is a measurement of who the loop is serving.

---

## How to judge a run

Read the transcript itself before the summary. `worker.py metrics` prints counts
and percentiles, and a percentile cannot tell you what an item waited for. The
log can. That is the map-and-territory distinction doing real work: when the
metric and the transcript disagree, the transcript is right, and no amount of
reading summaries will find the thing only the log shows. (A CI job here reported
success in 0.03 seconds for months; the summary was green and the log had every
command skipped.)

Then fill this in before deciding anything. Seven lines, no prose:

```
run:      <ids>            commits: N   rounds: R
cost:     worker Xcalls/Ys  review Xcalls/Ys  wall A->B
muda:     <category>: <what, and what it cost>     (one line each, only real ones)
mura:     <what varied against comparable runs>
muri:     <who was overburdened, with the number>
demand:   <what arrived, and how much of it arrived because something failed>
value:    <defects found, beads closed, gates now running>
```

Before the list, one preference. Deleting a step costs nothing; a retry, a longer
timeout or one more parallel chain costs every future run. Imai's version for a
factory: eliminating muda costs nothing while buying capacity costs money. So a
finding that can be answered by removing work beats one answered by adding
machinery, even when the machinery is the more obvious fix.

Then decide per finding. One that is a single edit made after the run gets fixed
now, and needs no bead, gate or rule behind it. Everything else goes through the
two questions in `DESIGN.md` section 10a, in the harness directory — can a
command catch it, and if not, would a general statement have caught the class? —
and the answers give a finding one of three homes, never a fourth:

1. **A deterministic check** when a command can catch it. A finding that comes
   back is a missing gate, not a careless worker. Two refinements, from
   mistake-proofing. First, prefer a device that makes the mistake impossible
   over one that reports it: a field a component cannot assign to beats a test
   that notices the assignment. Second, a device that only warns is not a device
   — a hook is skipped with `--no-verify` — so pair it with something that
   refuses. Either way, prove it by removing the exact thing it must catch, once
   per shape it must catch. `DESIGN.md` section 10 has the three levels.
2. **A general rule** when no command can catch it, but a general statement
   would have caught the class: a line in `AGENTS.md`, a skill,
   `docs/QUALITY.md`, or `docs/TESTING.md`, placed where the work happens. The
   statement, not the incident — "a mutation is restored from a copy" outlives
   the run that produced it, "do not use `git checkout --` here" does not.
   Undocumented conventions do not survive a session.
3. **Nothing** when neither holds: no command can catch it, and no general
   statement would have caught the class. One line in the summary, no edit, and
   no bead. A rule written for it anyway would name one incident, and the next
   reader would still have to decide whether it applied to them.

A gate or a rule this run cannot land is filed with `wm-5s`: a bead titled with
its 5S bucket *and* its muda category, `friction(seiton, waiting): ...`, carrying
an acceptance criterion that names a file or a command. The bucket says how to
fix it; the muda says what it costs. A finding in the third home is not filed,
because a bead asking for a gate no command can provide costs every session that
reads it. A run that found real defects can be long and still not be waste.

---

## Close the judgement with one target condition

A judgement that ends in a list of fixes gives the next cycle nothing to check
itself against. End it with one **target condition**: a condition on the process,
not on the result, observable, reachable in weeks, and carrying a date.

- "Reviews are faster" is a result. "By the 14th, every item in review has a
  recorded start time and a recorded decision time" is a target condition. Rother's
  point is blunt: a target that specifies only a result is not a target at all.
- Ask the five questions against the transcript rather than at a desk: what is the
  target condition; what is the condition now; which obstacle are you addressing;
  what is the next step and what do you expect to learn from it; when can we go and
  see?
- **The next step does not have to be a fix.** Very often it is to look more
  carefully, and a session told to produce a remedy before it has understood will
  invent one.
- When the judgement produced three or more findings, name the one that moves the
  target condition and give the rest their homes without acting on them. A cycle
  that changes five things cannot say which one worked, which is the same argument
  as one knob at a time.

---

## Two worked examples

**A run that was muda-free.** `wm-4f6.15` + `wm-dfg.25`: 82 worker calls, 8m40s,
one trivial error, first-review approval. It fixed a 2-byte overflow that had
survived twelve gates and added the gate that would have caught it, and it
proved the gate by re-breaking the fix. Judged by length it is unremarkable; by
category there is almost nothing to file. That is what a good run looks like.

**A run whose muda was in the harness, not the worker.** `wm-4f6.13`: 119 calls,
about 40 of them spent discovering that `scripts/xephyr-test.sh` rebuilt the
release binary over a `DEBUG=1` one. The worker did nothing wrong; the harness
lied by omission. The fix was a gate plus a variant line printed before the
harness starts — muda removed from every future run, not from that one.

---

## Applying it to a document, a skill, or a harness change

The same eight categories read against anything the project produces:

- a document that repeats another document is overproduction (the second code
  map in `AGENTS.md`, `CLAUDE.md`, `.cursor/rules/beads.mdc`);
- a check that never runs is a defect with inventory behind it: it sits in the
  list looking like coverage (the CI gates job, green in 0.03s);
- a rule written twice is mura: the two copies drift and the reader cannot tell
  which is current;
- a tool missing from the dev shell is unused talent: the worker has the skill
  and not the instrument, so it builds one from scratch.
