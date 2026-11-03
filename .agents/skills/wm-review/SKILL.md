---
name: wm-review
description: Review a worker's branch on a beads issue and leave a verdict. Use when asked to review work you did not write, when worker.py review runs you, or before merging a work/<bead> branch. Covers what to check, how deep, the verdict format, and the two allowed outcomes.
---

# Reviewing a worker's branch

You did not write this change. Your job is to find what is wrong with it, not to
agree with it, and then to leave a record that a later reader can act on.

This is not a gate. `docs/QUALITY.md` says why: a review that does not answer
the same way twice cannot block a merge. The gates do that. A review produces one
of two things instead: a commit that faces the gates like any other, or a
bead comment that the author reads when resumed.

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
| `src/xcb/`, `wm-xcb.c`, `src/components/client-list.c`, anything mapping or configuring windows | seam — the unit tests cannot see it, so the seam tests and a live run are the evidence. Distrust a green unit suite here |
| event loop, hub, state machines, `src/target/` | rules — the four commitments in `docs/PHILOSOPHY.md`. A state write outside a machine, a component calling another component, or anything blocking is a P1 |
| a new component or a new event/request | contracts — who owns the state, what refuses a transition, which event fires, what happens when the request fails |
| docs, scripts, `Makefile`, `.github/` | mechanics — does the command in the doc actually run; does the doc's description of its output match what it prints; does a count or a line number appear; does the new gate execute in CI |
| tests only | are they able to fail — a test that passes against the old code proves nothing. Run it against `master` if in doubt |

## What to look for, in this order

1. **Acceptance criteria, all of them, as written.** Quote each one and the code
   that satisfies it. A criterion quietly reinterpreted is the most common miss.
2. **A rule broken.** State machines own state; components do not call each
   other; nothing in the event loop blocks; new behaviour arrives as a
   registration. Each has a gate — run them: `make test`, `make check-async`,
   `./scripts/format-check.sh`, `./scripts/check-docs.sh`, and `make test-x`
   when the diff touches windows, keys, focus, or X properties.
3. **Something left broken or half-done and not mentioned.** A behaviour change
   without a test, a document that now contradicts the code, a comment
   describing what the code no longer does, a defect the author noticed and only
   put in their final message.
4. **Style, last.** Only where it hides a defect.

Not worth saying: the diff restated, praise, and anything the gates already
enforce. If the only finding is formatting, the gates will have caught it.

## The two allowed outcomes

**Fix what you are certain about.** A missing assertion, a doc that now
contradicts the code, a typo, a wrong symbol name. Commit it as one commit:

```
review: <what was wrong>

<which finding this closes, and why the fix is the one it is>
```

Then re-run the gates and record the verdict naming the new head.

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

## After a fix

Your commit is a normal commit: the pre-push gates run on it, and the human merge
is still the human's. Do not merge, push, rebase, amend, or close or reopen a
bead. If the author has to act on a finding, `worker.py resume <bead>` starts
them again with `bd comments` in front of them, which is the only reliable way
findings reach the model that wrote the branch.
