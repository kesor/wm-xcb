# Fuzz targets

Two harnesses, both built by `make fuzz` with clang, ASan and UBSan:

| Target | Entry point | What it looks for |
|---|---|---|
| `fuzz-xcb-event` | `xcb_handler_dispatch()` | a component handler reading past the 32-byte event the server sent: a count field that lies, a property type nobody expected, a `*_notify` read as the wrong struct |
| `fuzz-rule` | `config_should_float()`, `config_get_tags()` | the window-rule matcher on strings a client controls through `WM_CLASS` and `WM_NAME` |

Both are coverage-guided: libFuzzer keeps the inputs that reach new code, which
is why a crash comes with the exact bytes that produced it.

The corpus directories hold the seeds that were worth keeping. A crash prints
its input; add the file to `tests/fuzz/corpus/<target>/` and it becomes a
regression case that every later run re-tests.

The event harness runs every input in one process, and the hub's target registry
outlives each input. A RandR notify event creates a monitor; the harness destroys
the monitors an input created and fails when the registry does not come back to
where it started. Without that teardown the registry fills after 256 creations
and every later input takes the registry-full error path instead of the handler,
while the run still exits 0. `scripts/fuzz.sh` fails on that too, when the run
logs `Maximum number of targets reached`.

The event harness registers the RandR notify handler for response type 90, the
number the nested server reports when RandR starts at 89. A seed whose first
byte is 90 and whose subCode (byte 1) picks a change reaches the monitor path.
