---
name: wm-line
description: Decide what to optimize and what to refuse, using four rules — the route never changes, output cannot be counted, capacity is a choice, and waiting is a decision. Use when proposing a change to the loop, a new gate, a metric or a dashboard, a parallelism or batch policy, when weighing whether a technique borrowed from factory management, Lean or the theory of constraints applies here, or when a run produced a finding and the question is what to change rather than who was at fault.
---

# What to optimize, and what to refuse

`.agents/skills/wm-3m/SKILL.md` judges a run: what the flow lost, how uneven it
was, who was overburdened. `.agents/skills/wm-5s/SKILL.md` files the finding and
keeps the order. This skill sits upstream of both: it decides **what to optimize
in the first place**, and it refuses optimizations that do not hold in this
environment.

Four rules, named so they can be cited without a number. The reasoning behind
them, the numbers, the mechanisms the loop still lacks, and a glossary of every
standard term they borrow, are in `DESIGN.md` in the harness directory beside
this checkout (`../../../ai-workers-for-wm/` from a worktree). Read §1 and §2
there once. This skill is the working short form, and it states each term's claim
where the term first appears.

The four rules say what to refuse. They do not say which station to work on.
That is the next section, and it is the first thing to do before acting on any
of the four.

## Where the rules come from

Two factory systems are worth learning from, and copies of them have failed more
often than they have worked.

Henry Ford's assembly line needed one product at enormous volume, with every
station dedicated to one operation. Toyota could not do that: it made small
numbers of many different cars, so the same machines had to serve every kind.
Everyone at the time concluded a line was impossible in that situation. Taiichi
Ohno decided only Ford's *machinery* was tied to high volume, and the ideas
underneath were general, so he built different machinery for the same ideas. The
ideas: shorten the time from order to cash; build a mechanism that says when to
stop producing; stop judging stations by how busy they are; keep lowering the
limit until a problem shows and then fix that problem.

Eliyahu Goldratt's 2006 paper *Standing on the Shoulders of Giants* explains why
copies of Toyota's system failed in factories that were not Toyota's. Toyota's
machinery assumes three kinds of stability — the products and processes hold
still for a long time, demand for each product is steady, and the load on each
kind of resource is steady. Hitachi Tool Engineering, making 20,000 cutting tools
with new families every six months, had none of the three. Its repeated attempts
made performance worse, and the failure was neither ignorance nor laziness:
Toyota published everything. The machinery carried assumptions nobody had stated,
and the assumptions did not hold.

The lesson, which is the whole reason this skill exists: **take the reasoning,
then check the assumptions, then build the tools for this environment.** Copying
the tools is the step that failed.

## Which station to work on

The constraint is the station that sets the rate of the whole route: the one
that is longest and most often idle. Goldratt's five focusing steps reduce to
four here, because the fifth — go back and refuse to let inertia become the
constraint — is step 4 done again. Do them in order, and stop at the first one
you cannot do.

1. **Measure one item's chain.** Each station's time, in the route's order, for
   one item through the lane you are judging. The measurement lives in
   `DESIGN.md`, "The chain per item, measured", and only there. This file holds
   the rule; copying a number out of that section makes a second snapshot, and
   a snapshot copied forward is the drift that section warns about. Recompute
   the table before acting on it, and say which window you read.
2. **Name the station that is longest and most often idle.** That is the
   constraint. Longest alone is not enough: a station that is long but never
   waits has spare capacity, and the one that sits idle while work exists is
   the one the route is losing time to.
3. **Subordinate the rest of the route to keeping that station working.** Do
   not optimize a station that is not the constraint: work done there does not
   move the rate, and it lengthens the lead time of everything behind it. Do
   not let the constraint wait: the queue the "waiting is a decision" rule
   allows sits in front of this station and nowhere else.
4. **Re-measure after a change, and say which station is the constraint now.**
   A change that shortened a station may have moved the constraint to another
   one, and the next change aimed at the old one is work on a station that is
   no longer it. The new numbers go into `DESIGN.md`, not into this file.

## The four rules

### The route never changes

Every change takes the same five stations in the same order: work, gates, review,
merge, publish. Only the diff varies — its size, its kind, its difficulty — never
the route. No station is dedicated to a kind of change, which is exactly the
situation Ohno was in and exactly why his ideas apply here. Lean calls a sequence
like this **standard work**: the written current best way to do the job, so that
improvement has a baseline to be measured against.

*Allows:* small batches; pull rather than push; a stated limit on work in
process; reduce the setup instead of enlarging the batch; lower the tolerated
slack until a problem shows.

*Forbids:* explaining an uneven cycle by calling the loop unpredictable. The loop
is predictable. Something violated a limit or injected a wait, and both are
findable.

*Example:* one worker with five beads produced an 18-to-20-minute review at 70%
or more of the reviewer's context; the same reviewer on one bead took 10 minutes.
The batch enlarged the station's setup rather than spreading it over more output.
The fix was setup reduction — smaller diffs, a code map, a bootstrap skill — and
batching is right only where the reading is genuinely shared: beads on the same
file, or one-line document fixes. The prebuild step is setup done while the
station is not the constraint, which is the difference between setup that stops
the machine and setup that does not.

*Consequence:* the route is the factory, so editing the loop while a cycle is in
flight is rebuilding the line with a car on it. That is why harness edits happen
between cycles, with a re-exec afterwards. It is not a workaround.

### Output cannot be counted

No rate may become an objective. Lead time is the measure; value is a judgement
made per run.

*Why:* in a car factory a car is a car, so throughput can be counted and
improvement shows up as a number. Here the units are **incommensurable** — no
ratio converts one into another. A two-byte overflow fixed, plus the gate that
would have caught it, is worth more than a week of document fixes. Any "items per
night" number therefore rewards the cheap item and the shallow review, which is
**Goodhart's law**: a measure adopted as a target stops measuring what it was
chosen to measure, because the cheapest way to move the number becomes the work.

*Allows:* lead time as a measure — observable, comparable across runs, and it
does not need the units to be equal. A value judgement per run, which is the
`value:` line 3M requires. Lead time is a working stand-in for the **cost of
change**, what the next modification to this code will cost, which is the
quantity the project actually cares about and which no metric of the loop
captures.

*Forbids:* a dashboard whose top line is a count. Beads per hour, cycles per
night, commits per day. Such a number in a document is itself the defect, and the
fix is to delete it rather than explain it.

*Example:* an 18-minute review found two false claims copied into three
documents. By length, the most expensive review of that day; by value, the
cheapest way to find those two. Every rate-based measure would have scored it
worst and a 3-minute skim best.

### Capacity is a choice

While the loop runs below whatever is slowing it down, output equals how much
work is pulled in and nothing else. So choose the pull. When a real limit
appears, name it and make everything else serve it.

*Why:* a parallel chain on an unconstrained loop moves work into a queue; it does
not move more work out. **Little's Law** makes that an identity: work in process
equals throughput multiplied by lead time, so with throughput fixed by the slowest
station, more work in process means longer lead time by exactly that factor. The
same reasoning says what to do with spare capacity: pull more work with it, never
remove it. Goldratt's paper records plants that cut staff once the slack was
exposed, and every one of them ended worse than where it started.

*Allows:* raising parallelism on purpose, to find the limit. A **work-in-process
limit** with an actual number in it — the same decision **admission control**
makes at the door of a network. Staggering once a station is visibly saturated.

*Forbids:* tuning around a limit nobody measured, and treating unused capacity as
waste. **Headroom** is worth several times its arithmetic share near full
utilization: in the standard heavy-traffic approximation the queueing delay grows
roughly like `ρ/(1−ρ)`, so a station at 95% utilization has about twenty times
the wait of one at 50%. A policy built on a guess about the limit will be tuned
for a station that is not the one holding the loop back.

*Example:* this machine has 32 cores, and each chain runs a build, a unit suite,
a seam suite that starts nested X servers, and a review. Whether the first limit
is the CPU, the seam suite, the model endpoint or the review station is unknown.
Known: the loop has run a few chains at a time at most, so the limit is far away
and the pull is currently a preference, not a number.

### Waiting is a decision

Work waits on purpose in exactly one place: in front of whichever station is
slowest, so that station never sits idle while work exists. Everywhere else,
waiting is downtime somebody wrote into the loop, and each instance should be a
bead with its cost attached. Lead time is latency and items per night is
throughput; these rules set them in that order.

*Why:* the release rule is **backpressure** — the station that is behind tells the
front of the loop to stop letting work in. A factory buys a queue in front of its
slowest station because a starved station stops everything downstream. A queue
anywhere else is inventory: it lengthens lead time, hides problems and costs
money to hold. In a factory the buffer is per part, because parts repeat; here
nothing repeats except the route, so the buffer is per station. And unlike a
factory, this loop cannot hold finished work at all: an unmerged change decays and
an unreviewed one costs more than it saves, so the queue is the only inventory the
medium allows.

*Allows:* a deliberate queue in front of the saturated station, and filing every
other stall as friction under 5S with the duration.

*Forbids:* a stall with nothing working and the clock running. A fixed sleep where
a completion signal exists. Gates run over the same tree three times. A hand
resolution at merge. A branch left local-only while CI is red.

*Example:* four runs in one day ended with commits, no window and no verdict, and
each needed the work re-derived. Separately, 52 commits sat local-only for a day,
and behind them a CI job had been failing for months. The one-second polls are
not on this list: a station is working while the harness looks.

## The test: does the borrowed idea apply here

Before importing any technique — a muda category, a 5S bucket, a work-in-process
limit, a batch rule, a new gate, a dashboard — ask three questions in order, and
stop at the first one that fails.

1. **What is the reasoning, and what environment did the tool assume?** "Small
   batches" carries the reasoning "shorten the lead time". "Three colours of
   priority" carries "do not calculate more precisely than the data allows".
2. **Does that environment hold here?** Check against the four rules. If it needs
   stations dedicated to one kind of work, it was built for a high-volume line.
   If it needs the units to add up, it was built for a car factory. If it assumes
   a scarce resource or an unavoidable queue, this loop does not have that.
3. **What replaces it, and what is the witness?** Name the mechanism, the
   measure, and the run or bead that shows it working. A technique with no
   witness is decoration, and running decoration is not implementing anything.
   Say also which strength the replacement has, strongest first: impossible to do
   wrong, fails at the moment it is done, or an instruction someone has to
   remember. `DESIGN.md` §10 has the three levels and the difference between a
device that refuses and one that warns.

## Worked examples

**A gate.** "Add a gate for every class of bug we can think of" fails question 1:
a gate turns a finding into a permanent rule, and its assumption is that the
class recurs. Replacement: gate on an observed recurrence, and prove the gate by
removing the thing it must catch, once per shape it must catch. A gate that never
fires is a permanent tax nobody measured; a gate for a class never seen is a
guess.

**A metric.** "Report items merged per night so we can see the trend" fails
question 2 under "output cannot be counted". Replacement: lead time per item, and
the 3M value line, read together. When the trend and the value line disagree, the
value line is right.

**More parallelism.** "Run ten chains at once because we have 32 cores" fails
question 3: nothing says the CPU is the limit, and on an unconstrained loop the
extra chains only deepen a queue. Replacement: raise the count in a recorded run,
measure each station's time and queue depth, and name the station that degraded
first. If nothing degrades, the limit is further out and that is a result.

**A stall.** "The review sometimes takes 30 minutes; raise the timeout" fails
question 2 under "waiting is a decision". A timeout that fires is a signal, not a
setting. Replacement: find what the station was waiting for; if the wait was
bought on purpose, call it a queue and say in the bead why it is there.

**A bad night.** "Last night was much worse than the one before, so the change we
made is wrong" is **tampering**: reacting to variation the process always has.
The deadband is the fix. Act when a result is outside a band, not when it differs
from the run before it, and remember **regression to the mean** — an extreme run
is followed by a less extreme one with no change at all, which is why two runs in
a row are the weakest evidence in this skill.

**A board.** "Put up a board so everyone can see the work" is a technique. The
reasoning under it is that a visible queue lets the station behind it prepare
before an item arrives, and lets the loop stop pulling. The witness is one of two
things: a station's setup happens before the item reaches it, or a pull decision
changes because the queue was visible. Without one of those it is decoration, and
an insider's account of Toyota is blunt about it: the boards, charts and cards are
for starting a conversation, and "just making things visual is not effective".

**A device that warns.** "The harness refuses to edit `worker.py` during a cycle"
reads like a rule but is an instruction: it relies on somebody remembering, and it
has already been broken. The replacement, in order of strength: a lock the edit
path refuses while a cycle is live, then a check at the moment of the edit, and
only then a sentence in a document. A written rule is the weakest of the three and
it is what gets written when the first two were not tried.

## Boundaries

- Not a licence to add instrumentation. A measure is added when a rule cannot be
  read without it, and `DESIGN.md` §7 names those and their order.
- Not an argument for a small pull everywhere. The rule says choose the pull, not
  that the number is small.
- Not a second ladder for findings. Findings go through 3M's ladder and 5S's
  buckets unchanged; this skill only decides which optimizations are worth
  pursuing.
- The one-line version: **lead time is the measure, value is a judgement, the
  pull is a choice, and every wait that is not the queue in front of the slowest
  station is a bead.**
- Every standard term this skill uses, and the one-line claim it makes, is in
  `DESIGN.md` §12. Cite the term when it saves a paragraph; state the claim when
the reader may not know it. A term used as shorthand for something we never
  checked is the failure this whole approach exists to avoid.
