## What changes, and for whom

## Why

## Rules exercised

Which rule in [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md#3-rules-that-follow-and-where-they-are-enforced)
does this change exercise, and how did you check it? If it adds behaviour, say
which extension point it registers with. If it edited a file it does not own,
name the missing extension point and why adding it here was the right shape.

## Checks

Tick a line only after running it. Say which one you did not run and why.

- [ ] `make test`
- [ ] `make test-x` when the change touches map, tile, focus, or a property
- [ ] `scripts/check-async.sh`
- [ ] `scripts/format-check.sh`
