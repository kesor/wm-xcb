---
name: wm-review
description: Review a worker's branch on a beads issue and leave a verdict. Use when asked to review work you did not write, when worker.py review runs you, or before merging a work/<bead> branch. Covers what to check, how deep, the verdict format, and the two allowed outcomes.
---

# Reviewing a worker's branch

You did not write this change. Your job is to find what is wrong with it, not to
agree with it, and then to leave a record that a later reader can act on.

This is not a gate. `docs/QUALITY.md` says why: a review that does not answer
the same way twice cannot block a merge. The gates do that, and they run at the
merge, after this verdict: `land` rebases, then runs the pre-push set, then
merges. An approval therefore does not make a tree pass them. The commands
listed below are a subset of that set. Skipping one does not fail the merge. If
the tree would fail that command, the review did not see the failure, and the
merge's refusal costs another cycle on the same diff. Running every listed
command still leaves the rest of the pre-push set for the merge. A review
produces one of two things instead: a commit that faces the gates like any
other, or a bead comment that the author reads when resumed.

## Inputs

- the bead id (and its acceptance criteria),
- the branch `work/<bead>` in a git worktree, based on `master`,
- the model that wrote it (you are a different one on purpose).

Write the head sha down before you read anything:

```bash
git -C <worktree> rev-parse --short=7 HEAD
```

Copy it from git. A sha written from memory parses as cleanly as one copied, and
it is a claim about a commit that may not exist.

## Work from the diff, not from the description

The author's final message is a claim about the diff. Read the diff:

```bash
git -C <worktree> log --oneline master..HEAD
git -C <worktree> diff master...HEAD
```

A description that overstates what was checked is the defect that costs the next
reader the most time. If the message says "tested under Xephyr", look for the
run; if it says "docs updated", read the doc.

## How deep, by what the diff touches

| The diff touches | Read it like |
|---|---|
| `src/xcb/`, `wm-xcb.c` (at the checkout root, not under `src/`), `src/components/client-list.c`, anything mapping or configuring windows | seam — the unit tests cannot see it, so the seam tests and a live run are the evidence. Distrust a green unit suite here |
| event loop, hub, state machines, `src/target/` | rules — the four commitments in `docs/PHILOSOPHY.md`. A state write outside a machine, a component calling another component, or anything blocking is a P1 |
| a new component or a new event/request | contracts — who owns the state, what refuses a transition, which event fires, what happens when the request fails |
| docs, scripts, `Makefile`, `.github/` | mechanics — does the command in the doc actually run; does the doc's description of its output match what it prints; does a count or a line number appear; does the new gate execute in CI |
| tests only | are they able to fail — a test that passes against the old code proves nothing. Run it against `master` if in doubt |

## What to look for, in this order

1. **Acceptance criteria, all of them, as written.** Quote each one and the code
   that satisfies it. A criterion quietly reinterpreted is the most common miss.
2. **A new check can fail, proven by removing what it must catch.** The author's
   word is not the proof: remove the exact thing the check is for, once per
   shape it must catch, and watch it fail. For a table walker that is one
   removal per shape of row: a plain row, a self-loop, a row with a guard, a row
   with no event. "I mutated something and it failed" is not that. A copy of the
   script in a temp tree is not executable: run it with `bash` or `chmod +x` it,
   and read a 126 as the copy's mode, not as the check catching the removal.
   Restore each mutation from a copy of the file, not with `git checkout --`:
   the file you mutated is often one you are also fixing, and checkout restores
   the author's version and takes your fix with it. `cp f /tmp/f.ok` before the
   first mutation and copy it back, or commit the fix before you mutate.
3. **A rule broken.** State machines own state; components do not call each
   other; nothing in the event loop blocks; new behaviour arrives as a
   registration. Each has a gate — run them: `make test`, `make check-async`,
   `./scripts/format-check.sh`, `./scripts/check-docs.sh`, and `make test-x`
   when the diff touches windows, keys, focus, or X properties, or when the bead
   is about them and the branch carries no diff to read. A manual-session bead
   that approved 22 of 32 keybindings was reviewed with no seam run, on an empty
   diff, and the seam suite was the evidence that did not depend on the author's
   throwaway scripts. The next section names every rule in `docs/PHILOSOPHY.md`
   section 3, what a violation of it looks like, and how to check it.
4. **Something left broken or half-done and not mentioned.** A behaviour change
   without a test, a document that now contradicts the code, a comment
   describing what the code no longer does, a defect the author noticed and only
   put in their final message.
5. **Style, last.** Only where it hides a defect.

Not worth saying: the diff restated, praise, and anything the gates already
enforce. If the only finding is formatting, the gates will have caught it.

## The philosophy rules, as checks

`docs/PHILOSOPHY.md` section 3 is the list. A rule with no command behind it is
still a rule, and a review is where it gets checked. Walk every row, not only
the rows the diff appears to touch: a rule usually breaks in a file the change
did not mention.

| Rule | A violation looks like | How to check it |
|------|------------------------|-----------------|
| No component calls another component | one component includes another's header or calls its functions; a request that exists only as a direct call | read the callers. `hub_send_request()` and `hub_subscribe()` are the only paths between components. Reading a state machine another component stored on the same target is allowed (2.1) |
| No state assignment outside `sm_raw_write` / `sm_transition` | `client->mapped = true`, `selected_monitor = ...`, or any field a template tracks written by hand | `scripts/check-private-state.sh`. A tracked field with no `tests/compile-fail/` fixture is still a read of the diff |
| No `_reply()` in a handler or action | `xcb_get_property_reply()`, `xcb_get_geometry_reply()`, or the plural form, inside anything an event can reach | `make check-async`. The reply belongs in a `wm_loop_add_poll_handler()` callback |
| No blocking call in the loop | `waitpid()`, `read()` on a child, `sleep()`, or a loop that calls `xcb_poll_for_event()` and never blocks | `make check-async` for the calls; `scripts/perf-test.sh` for idle CPU, which is what a spin costs |
| One flush per loop iteration | `xcb_flush()` after a request, in a handler, or in an action | read the loop in `wm-xcb.c`: one flush, after the handlers ran |
| Every request has a failure event | an executor that returns on a refused guard and emits nothing, so a subscriber cannot tell refusal from success | read the executor and its event table against `EVT_FULLSCREEN_FAILED` in `src/components/fullscreen.h` and `EVT_FLOATING_FAILED` in `src/components/floating.h` |
| New behaviour is a registration, not an edit | a feature that edits a component it does not own instead of adding a request, an event, a subscriber, an SM template, or an action | run the extension point test (section 4): name the file and the missing extension point. New per-target state belongs in the target that owns it, and that is not a collision |
| Event ids come from the shared registry | a fresh integer in a local `enum`, or a base that overlaps another component's range | `src/event-types.h` for the range, `tests/unit/test-event-types.c` for a collision |
| `make test` green, `scripts/xephyr-test.sh` no worse | a failing assertion, or a new seam failure missing from `tests/quality/seam-baseline.txt` | `make test`, `make test-x`, and `scripts/xephyr-test.sh` when the diff touches map, tile, focus, or a property |
| No AI attribution | `Co-Authored-By`, a "generated with" footer, or a robot signature in a commit, comment, doc, or PR body | read the commit messages with `git log` |

Run every command a row names, and say in the verdict that it ran. A row with
no command is what the read of the diff is for.

## The two allowed outcomes

**Fix what you are certain about.** A missing assertion, a doc that now
contradicts the code, a typo, a wrong symbol name. Commit it as one commit:

```
review: <what was wrong>

<which finding this closes, and why the fix is the one it is>
```

Then re-run the gates and record the verdict naming the new head.

Your fix is held to the rule the author's fix is: it arrives with a test that
fails without it. A review commit that fixed the order two `_NET_WM_STATE`
replies are applied in shipped with no test, and the reviewer had written the gap
into the verdict before committing anyway. `make test` and `make test-x` passed
on both the broken walk and the fixed one, so reading the diff was the only thing
keeping it. When the fixed code cannot be reached by a test - a `static`
function no seam can see, a branch that needs a server the suite does not start -
commit it only with a bead that says why it has no test and what would make one
possible. Naming the gap in the verdict is not a substitute for the bead.

**Record everything else.** Anything needing a design decision, anything you are
not sure about, anything that would change what the author chose to build. Change
no code. One comment, findings worst first.

Never leave the tree dirty. `review` fails if you do, because a review ends in a
commit or in a comment.

## The verdict

`worker.py review` runs you with `bd comment` available; write the verdict
yourself at the end of your turn. The first line is the verdict and the sha it
covers, because a reader who stops there has to know where they stand:

```
Approved at 4f6a1c2.
```

or

```
Changes requested before merge at 4f6a1c2.
```

Then the findings, worst first, then what you checked and what you did not.

- Name the file and the symbol, not a line number: `client_list_map_client()`,
  `EVT_CLIENT_MANAGED`. A bead comment may cite a line because it is a snapshot;
  a document may not.
- Say what you did not check. "Reviewed the C diff and ran the gates; did not
  exercise the play in Xephyr" is the sentence that lets the next reader trust
  the rest.
- A verdict covers one commit. If the branch moves after you write it, the
  verdict is stale and the reader cannot tell — so name the sha, every time.
- If you committed a fix, name both: the sha you read the author's work at, and
  the sha your commit produced.

The last line of this comment is the close decision, alone on its line, so a
match does not have to read a sentence:

```
Close: no
```

or

```
Close: yes
```

`Close: no` means the branch lands and this bead stays open. It does not cover
the other beads the worktree carried. The finding above it names the bead that
carries the remaining cause. `Close: yes` is the usual case: the branch is the
whole fix, so the merge closes the bead. A missing line means close. A sentence
in the body is not a decision.

`land` reads the line. A `Close: no` still rebases, still runs the pre-push
gates, still merges the branch, and still removes the worktree and the branch;
then it sets that one bead back to `open` with a comment pointing at the
findings, so `bd ready` offers it again. `publish` leaves it open after the pull
request lands. Every other id the worktree carried still closes, and a missing
line means close. Before land read the line, on 2026-11-03, a verdict said "Do
not close this bead on the strength of the branch alone", the merge closed the
bead, and the status read merged. A comment at 04:47 UTC, after the 04:44 close,
named the bead that still carried the cause. This review does not change the
status.

## After a fix

Your commit is a normal commit: the pre-push gates run on it, and the human merge
is still the human's. Do not merge, push, rebase, amend, or close or reopen a
bead. If the author has to act on a finding, `worker.py resume <bead>` starts
them again with `bd comments` in front of them, which is the only reliable way
findings reach the model that wrote the branch.
