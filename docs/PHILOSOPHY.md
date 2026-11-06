# Philosophy

*Written 2026-11-01. Every rule below says what to do in code, what the person
using the window manager gets out of it, and what it costs. When a rule and the
code disagree, the code is wrong and the rule stands.*

> **Why this file exists:** wm's advantages are not developer comfort. dwm is
> easier to read, smaller, and works today. wm's bet is that a user gets
> something dwm cannot give: a window manager whose behaviour does not change
> behind their back when a new feature lands. That bet is only worth anything if
> the rules that support it are written down and followed. This is that list.

---

## 1. What a user should be able to count on

These are the promises. Each is testable, and each has a current status, because
a promise the code does not keep is a defect list, not marketing.

| Promise | What it means for the person using the WM | Status 2026-11-01 |
|---------|-------------------------------------------|-------------------|
| **Windows appear where the WM said they would** | a new window is mapped, sized, and placed by the WM, not left invisible | kept for new windows: mapped, tiled, and placed on `MapRequest` |
| **The WM never freezes while an application is slow** | a hanging client, a slow `dmenu`, a busy X server does not stop the loop from running | kept (2026-11-04): the loop keeps running while dmenu is open, and no handler or action waits for the server (2.3). While the prompt is up, dmenu holds the keyboard grab itself, so the keys go to it |
| **A key means the same thing everywhere** | `MOD+1` shows tag 1 whether the focused window is a terminal, a browser, or nothing | not kept: no key grabs exist (4.5) |
| **What worked yesterday still works after an update** | behaviour is pinned by tests, so an unrelated feature cannot silently change the tiling or the focus rule | partly kept: the unit suite covers the model, `make test-x` covers the X seam and still fails there |
| **The WM can explain itself** | current state and recent transitions can be dumped and read | not kept: `LOG_DEBUG` only, no state dump or event trace |

Those five are the product. Everything below exists to keep them.

### The other promise: you should never need a C compiler

dwm is a developer's window manager. Colors and keybindings live in `config.h`,
so changing them means a rebuild; anything more is a patch from a wiki, applied
with `patch`, and stepping on the other patches. That is the same criticism this
project makes about patch conflicts, seen from the user's side instead of the
maintainer's.

wm is no better today: gaps and borders are still compiled into `config.def.h`
and `config.wm.def.h`. Keybindings, float rules and tag names come from a text
file (`docs/configuration.md`), and a binding that names an action nobody
registered is reported once at startup with its key and the action name. The
float rules and the tag names are read and kept, and nothing consults them yet.
So this is a goal with a status column, not a claim.

| Goal | Status 2026-11-04 |
|------|-------------------|
| No config file needed: defaults that work out of the box | not kept: `terminal.spawn` defaults to `xterm`, so `MOD+Return` runs nothing on a machine without it |
| Change keybindings, rules, and tag names in a text file, no rebuild | partly kept: a `bind` line changes which action a key invokes without a rebuild; `float` and `tags` lines are read, but the manage path never asks the float rules and nothing creates the tag list at startup |
| Reload that file without restarting | not kept: the file is read once, at startup |
| A wrong line in that file says what is wrong, once, at startup | partly kept: a line the parser cannot read is reported with its path and line number, and a binding to an action nobody registered is reported once with its key and the action name |
| Every feature a dwm patch provides is a config toggle here | not kept |

**The rule that follows:** a feature a user turns on is a config key, not a
`#define` and not a patch. If the only way to enable it is to edit our source,
it is not finished. That rule is also what makes the extension point test in §4
worth anything: an extension point nobody can reach without a compiler is not an
extension point.

---

## 2. The four commitments

### 2.1 Components do not call each other

A component never includes another component's header and never calls its
functions. It sends a request through the hub and subscribes to events. The hub
owns the registry, the request router, and the event bus.

- **In code:** `hub_send_request(REQ_*, target)`, `hub_subscribe(EVT_*, handler)`,
  `hub_register_component()`. No `#include "focus.h"` from inside `tiling.c`.
- **For the user:** a component does not call another component's functions.
  It can still read a state machine another component stored on the same
  target. Tiling reads the fullscreen and floating machines by name, so a
  change in one machine can change what another component does with that
  target, without a request between them.
- **What it costs:** every call that used to be a function call is now a lookup
  and an indirect dispatch, and reading a behaviour means following an event
  chain instead of one call stack.

**Honest limit:** decoupling pays only if the extension points are wide enough.
See §4 — a feature that needs new state still edits the target that owns it.
Adoption is decided when a target registers: the hub calls `on_adopt` on the
components already registered, and has no second pass for a target that already
exists. A component registered later is adopted only by targets created after
it. The hub can replace what a request or an event does; it cannot swap a
component into a running target.

### 2.2 State machines own state

Anything with mutually exclusive values is a state machine living on the target
it describes: `FullscreenSM` and `FocusSM` on a `Client`, `TagViewSM` and
`LayoutSM` on a `Monitor`. Components supply templates; targets own instances.

Two kinds of write, and the difference matters:

| Write | Used when | Guard | On failure |
|-------|-----------|-------|------------|
| `sm_raw_write()` | reality changed: the X server said so | bypassed | cannot fail |
| `sm_transition()` | someone intends a change: a keybinding, a plugin | consulted | state unchanged, a failure event is emitted |

- **In code:** state is only changed through these two calls. `client->fullscreen = true`
  is always wrong. `StateMachine` and the client's tracked values are defined in
  headers no component includes, so assigning those fields does not compile.
  The values that stay reachable, and why, are in
  `docs/architecture/state-machine.md`.
- **For the user:** the WM and the X server can never quietly disagree. When a
  request cannot be honoured, the user gets a failure event instead of a lie.
- **What it costs:** a state machine is more code than a boolean, and a badly
  designed one is worse than the boolean. Hence §5.

### 2.3 Async first

No code in the event loop may wait. Not for X, not for a child process, not for
a pipe. Requests are fire-and-forget with a correlation id; the answer arrives
later as an event.

- **In code:**
  - never call `xcb_*_reply()` from an XCB handler or an action callback;
    use the cookie and process the reply when it arrives, or restructure so no
    reply is needed
  - a reply is read from a callback registered with
    `wm_loop_add_poll_handler()`, which runs once per iteration and polls
    rather than waits
  - one `xcb_flush()` per loop iteration, not one per call
  - the main loop blocks on `poll()` or `xcb_wait_for_event()`, never spins
  - child processes (launcher, terminal) are `fork` + `exec`. A `SIGCHLD`
    handler, when one is installed, may only call async-signal-safe functions
    and `waitpid(WNOHANG)` — not `LOG_*` and not `signal()` to reinstall itself
- **For the user:** a stuck application, a slow dmenu, or a hotplug storm cannot
  stop the WM from handling keys. This is the difference between "my window
  manager froze" and "that app froze".
- **What it costs:** callbacks where a straight-line function would do, and
  lifetimes that have to be reasoned about (`req->data` must not outlive the
  executor).

**Checked by** `make check-async` (`scripts/check-async.sh`), which fails on
blocking calls unless the line carries `allow-blocking: <reason>`, and refuses
that reason inside a function an XCB handler can reach.

**Current debt:** none. `tests/quality/async-baseline.txt` is empty: every wait
left in the tree is a startup path carrying `allow-blocking: <reason>`, or the
non-blocking read of a pipe, and a new violation fails the check.

The launcher is not on that list any more. It forks a shell whose last stage is
`dmenu`, with `dmenu_path` feeding it, and registers the read end of that
pipeline's stdout with the loop as an event source. The promise holds while the
pipe exists: the only read of it happens in the loop's descriptor callback, when
`poll(2)` reports it, and a prompt that closes with nothing written emits a
failure event. A child is reaped by the `SIGCHLD` handler, never by the code
that started it.

### 2.4 Behaviour arrives as a registration, not as an edit

New behaviour should be added by registering something: a component, an XCB
handler, an action, an SM template, an event subscriber. Not by editing a shared
function that other features also edit.

This is the one commitment that dwm cannot make, and the reason wm exists.
On the dwm fork this project was measured against, `arrange()` is edited by 13
of its 32 patches, `focus()` by 11, `tile()` by 9. Two dwm extensions that both need `arrange()`
will conflict, and the user pays for that conflict in a fork nobody upstreams.

**Honest limit — read this before claiming the advantage:** a feature that needs
new per-client state still edits the target that owns it (`src/target/client.c`),
and a feature that changes how windows are stacked still edits the component
that stacks them. Registrations only help for the part of a feature that is
"react to X" or "change Y when Z happens". So:

> **The extension point test.** If a feature must edit a file it does not own,
> that file is missing an extension point. Name the file, name the missing
> extension point, and add it before adding the feature. If the feature really
> is new core state, it belongs in the target that owns that state, and it does
> not count as a collision because targets are the documented place for it.

Run that test on every extension design in `docs/extensions/` and the result is
a list of extension points to build. That list, not the file layout, is what
makes the decoupling claim true. The audit and its current result live in
[extensions/README.md](extensions/README.md#status-and-the-extension-point-audit).

---

## 3. Rules that follow, and where they are enforced

Short enough to check in review.

| Rule | Why | Where it is checked |
|------|-----|---------------------|
| No component calls another component | 2.1 | review, by a person or an agent: `.agents/skills/wm-review/SKILL.md` |
| No state assignment outside `sm_raw_write` / `sm_transition` | 2.2 | `scripts/check-private-state.sh` refuses the assignments in `tests/compile-fail/`. A tracked field with no fixture there is still review |
| No `_reply()` in a handler or action | 2.3 | review + the async audit issue |
| No blocking call in the loop: `poll`/`wait_for_event` only | 2.3 | `scripts/perf-test.sh` (idle CPU) |
| One flush per loop iteration | 2.3 | review |
| Every request has a failure event | 2.2 | review: a request with only a success path is incomplete |
| New behaviour is a registration, not an edit (see §2.4 test) | 2.4 | review |
| Event ids come from the shared registry, never a fresh local enum | two components that share a value receive each other's events | `src/event-types.h`, `tests/unit/test-event-types.c` |
| `make test` green, `scripts/xephyr-test.sh` no worse than before | promise 4 | CI (planned) |
| No AI attribution in commits, comments, or files | project rule | review |

---

## 4. The extension point test in practice

Three questions, in order:

1. **Can the feature be expressed as subscribe + request + own SM template?**
   Then it adds files only. Good.
2. **Does it need state that no target owns yet?** Then it introduces a target
   type (or adds a state machine to an existing target) and registers it with the
   hub. That is the documented place for new state, and it is still additive.
3. **Does it need to change how an existing component does its job?** Then the
   answer is usually no: the component needs a hook, a new request, or a new
   event. Add that, and say so in the issue. Editing the component directly is
   the last resort, and it must be justified in the commit message.

A feature that reaches step 3 repeatedly is telling us the component boundary is
wrong, not that the feature is unusual.

---

## 5. The state machine contract (the bet we lean on)

The state machines are the part of wm worth defending, so they get stricter rules
than anything else. The goal is that a reader can look at a template and know
every reachable state and every way out of it, without reading guard bodies.

**Design rules**

1. **One machine per concern.** If two values can change independently, they are
   two machines. A machine whose states are the cross product is a smell.
2. **States are nouns in the past or present, not verbs.** `FULLSCREEN` and
   `WINDOWED`, not `ENTER_FULLSCREEN`. Transitions are the verbs.
3. **Transitions are declared, never implied.** Every reachable pair appears in
   the template. If `sm_transition()` succeeds on a pair that is not in the
   table, that is a bug.
4. **Guards are pure and total.** No X calls, no allocation, no logging inside a
   guard; they read data and answer yes or no. A guard that can fail for a reason
   the caller cannot see is a guard that will be debugged in production.
5. **Actions are idempotent.** Running an action twice must leave the same state.
   Actions may issue X requests; they may not change other state machines.
6. **Every transition emits exactly one event.** Subscribers are the only way the
   rest of the system learns about state, so a silent transition breaks them.
7. **Reality writes raw, intent writes through the guard.** An X event that
   contradicts the machine is a `raw_write`, and the machine accepts it. A
   keybinding is a `transition`, and it may be refused.
8. **Hooks are for observers, not for policy.** `PRE_GUARD`/`POST_ACTION` etc.
   may log, trace, or update a cache. Policy belongs in a guard.
9. **No machine for a value with no alternatives.** A counter is not a state
   machine. The rule is "mutually exclusive values", not "any variable".
10. **Every template ships with its diagram.** A doc comment listing states and
    transitions, in text, in the header next to the template. If the diagram is
    hard to write, the machine is wrong.

**What this buys the user:** a WM whose focus and layout rules can be stated in a
few sentences and cannot drift from the code, plus the ability to answer "why is
this window fullscreen?" by dumping one machine's state and its last transitions.

**What it costs:** the discipline in the ten rules above, and the willingness to
split a machine rather than add a state.

**Work this implies:** a trace facility (`kill -USR1` dumps every live machine's
state and last N transitions), a `sm_template` definition format that reads as a
table, and a test for each machine that walks every declared transition. See the
beads epic for state machine ergonomics.

---

## 6. What is deliberately not a goal

- **Being small.** dwm is a few thousand lines and works; wm is several times
  that and does not yet. Compare them with `wc -l` if the ratio matters.
  Smaller is a real virtue and wm does not have it. The bet is that
  composability is worth the size, and that size stays bounded because features
  are additive rather than interleaved.
- **Zero dependencies.** libxcb and friends are fine.
- **Runtime plugin loading.** Compile-time components only, for now
  (`plans/wm-architecture.md`, out of scope for v1). The decoupling is about
  replaceability, not `dlopen`.
- **Config reload without a restart.** Not yet written. The file is read once, at
  startup, so an edit needs a restart.
- **Beating dwm's latency.** The target is "not worse". A WM that freezes is
  worse; see §2.3.

---

*Related: `docs/VISION.md` (the story), [README.md](README.md#current-state)
(what works today), `docs/architecture/state-machine.md` (the framework),
`docs/TESTING.md` (how to check all of this).*
