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

## How to judge a run

Read the transcript itself before the summary. `worker.py metrics` prints counts
and percentiles, and a percentile cannot tell you what an item waited for. The
log can. That is the map-and-territory distinction doing real work: when the
metric and the transcript disagree, the transcript is right, and no amount of
reading summaries will find the thing only the log shows. (A CI job here reported
success in 0.03 seconds for months; the summary was green and the log had every
command skipped.)

Then fill this in before deciding anything. Six lines, no prose:

```
run:      <ids>            commits: N   rounds: R
cost:     worker Xcalls/Ys  review Xcalls/Ys  wall A->B
muda:     <category>: <what, and what it cost>     (one line each, only real ones)
mura:     <what varied against comparable runs>
muri:     <who was overburdened, with the number>
value:    <defects found, beads closed, gates now running>
```

Before the list, one preference. Deleting a step costs nothing; a retry, a longer
timeout or one more parallel chain costs every future run. Imai's version for a
factory: eliminating muda costs nothing while buying capacity costs money. So a
finding that can be answered by removing work beats one answered by adding
machinery, even when the machinery is the more obvious fix.

Then decide per finding, cheapest first:

1. **Fix it now** if it is one edit and the run is over.
2. **Gate it** if a command can catch it. A finding that comes back is a
   missing gate, not a careless worker. Two refinements, from mistake-proofing.
   First, prefer a device that makes the mistake impossible over one that reports
   it: a field a component cannot assign to beats a test that notices the
   assignment. Second, a device that only warns is not a device — a hook is
   skipped with `--no-verify` — so pair it with something that refuses. Either
   way, prove it by removing the exact thing it must catch, once per shape it must
   catch. `DESIGN.md` §10 in the harness directory has the three levels.
3. **Write it down** if the fix is a rule: a line in `AGENTS.md`, a skill, or
   `docs/QUALITY.md`. Undocumented conventions do not survive a session.
4. **File it** as a bead when none of the above fits, with the 5S bucket *and*
   the muda category in the title: `friction(seiton, waiting): ...`. The bucket
   says how to fix it; the muda says what it costs.
5. **Drop it** if the value was real. Not every long run is waste.

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
  target condition and file the rest. A cycle that changes five things cannot say
  which one worked, which is the same argument as one knob at a time.

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
