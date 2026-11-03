# wm-xcb

wm-xcb is an X11 window manager written in C over libxcb, built as a dwm
alternative: a feature is a component that subscribes to hub events and sends
requests, and state lives in state machines on targets, so an extension does not
patch shared code. It is not a daily driver yet. Windows are mapped, tiled and
focused, keys are grabbed and the pointer is tracked, but it sets no EWMH root
properties and every new client lands on tag 1 whatever the tag view says;
[docs/README.md#current-state](docs/README.md#current-state) keeps that list,
and each gap in it is a beads issue.

## Build and run

```bash
direnv allow          # or: nix develop — gcc, the libxcb packages, the test tools
make hooks            # once per clone: points git at .githooks
make                  # builds ./wm
./wm                  # connects to $DISPLAY; takes no arguments
```

Outside the dev shell, install what `PKGLIST` in the Makefile asks `pkg-config`
for — `xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms` — plus
gcc, make and pkg-config.

| Target | What it does |
|--------|--------------|
| `make` | builds `./wm`; objects go under `build/<variant>/` |
| `make test` | unit suite: the model, no X server needed |
| `make test-x` | seam suite: a real WM in a nested X server |
| `make format` | clang-format every C file and header |

`make DEBUG=1` builds the `-pedantic` variant and is the only one that turns
`LOG_DEBUG()` into output. `docs/TESTING.md` has that build, the nested session,
the sanitizer runs and the performance harness.

Configuration is compiled in. Keybindings are wired in `config.wm.def.h` to
action names from the registry; tags, window rules, gaps and border colours are
in `config.def.h`. Edit, then `make` again.

## Where to go next

| Document | Read it for |
|----------|-------------|
| [docs/README.md](docs/README.md) | the doc index, and the current state |
| [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) | the four commitments, and the promises to the person using the WM |
| [docs/architecture/overview.md](docs/architecture/overview.md) | hub, targets, components, the two kinds of write |
| [docs/architecture/](docs/architecture/README.md) | the rest of the design, including the decision log |
| [docs/QUALITY.md](docs/QUALITY.md) | the gates that run on commit and push, and how to add one |
| [docs/TESTING.md](docs/TESTING.md) | build variants, both test suites, measurement |
| [AGENTS.md](AGENTS.md) | what a change has to satisfy, for a person or an agent |
| `bd ready` | the unblocked work, with each issue's acceptance criteria |

## Contributing

Two rules are not guessable from the code:

- **No AI attribution anywhere.** No `Co-Authored-By` trailer, no "generated
  with" footer, no robot signature in a commit, a comment, a pull request or a
  doc. The git author is the only attribution.
- **No beads id in source, a script, a Makefile or a CI config.** Write the
  reason the code is this way; a reader of the file does not have the tracker
  open, and an id explains nothing.

`make hooks` installs the pre-commit and pre-push gates; a gate that fails is a
defect to fix, not something to skip with `--no-verify`.
