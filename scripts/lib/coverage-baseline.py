#!/usr/bin/env python3
"""coverage-baseline.py - merge one coverage run into the per-file floors.

scripts/check-coverage.sh measures and prints the table; this decides what the
run does to tests/quality/coverage-baseline.txt. The split exists so the rules
can be read and pinned (tests/x/test-111-coverage-baseline.sh) without gcov, a
build, or the tree.

Usage:
  coverage-baseline.py check  --measured F --baseline B --root R [--pattern P]...
  coverage-baseline.py update --measured F --baseline B --root R [--allow-lower]
                              [--margin M] [--pattern P]...

--measured is one `name<TAB>lines-percent` line per file the run executed.
--pattern, repeated, restricts the work to the named files: a path relative to
the root, or a suffix of one, so `config-file.c` names src/config-file.c.

A file under tests/ carries no floor here, in the check or in the update. Its
missed lines are the guards inside the tests' own helpers, and no passing test
reaches one without breaking its environment.

Exit status: 0 when the run meets or merges into the baseline, 1 otherwise.
"""

import argparse
import os
import sys

PREFIX = "check-coverage: "

# The floor written is the measured value less this many points: the suite is
# not perfectly deterministic. The launcher tests fork and exec, and the child's
# path is covered depending on scheduling. One file moved 0.9 points between two
# runs here and 2.6 points on a CI runner.
DEFAULT_MARGIN = 3.0

HEADER = [
    "# Line-coverage floors for the unit suite, per production source file.",
    "# Written by scripts/check-coverage.sh --update-baseline. A file that",
    "# drops below its floor fails the gate; a new file must be added here",
    "# deliberately, which is the moment somebody decides what covers it.",
    "# Test files under tests/ are printed by the run and carry no floor: a",
    "# helper's error return is only reachable by breaking the environment.",
    "# Deleted files are dropped by the next update.",
    "#",
]


def carries_floor(name):
    return not name.startswith("tests/")


def read_measured(path):
    measured = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            name, _, pct = line.rpartition("\t")
            if not name:
                raise SystemExit(f"{PREFIX}{path}: '{line}' is not name<TAB>percent")
            measured[name] = float(pct)
    return measured


def read_baseline(path):
    names, floors = [], {}
    if not os.path.exists(path):
        return names, floors
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            if parts[0] not in floors:
                names.append(parts[0])
            floors[parts[0]] = float(parts[1])
    return names, floors


def normalise(pattern):
    pattern = pattern[2:] if pattern.startswith("./") else pattern
    return pattern.rstrip("/")


def matches(name, patterns):
    for pattern in patterns:
        p = normalise(pattern)
        if name == p or name.endswith("/" + p):
            return True
    return False


def selected(name, patterns):
    return not patterns or matches(name, patterns)


def unmatched_patterns(patterns, measured, floors):
    known = list(measured) + list(floors)
    return [p for p in patterns if not any(matches(n, [p]) for n in known)]


def report_unmatched(patterns, measured, floors):
    missing = unmatched_patterns(patterns, measured, floors)
    if not missing:
        return 0
    for pattern in missing:
        print(
            f"{PREFIX}{pattern} names no file this run measured and no file in the"
            " baseline; the floor is unchanged",
            file=sys.stderr,
        )
    return 1


def check(args):
    measured = read_measured(args.measured)
    _names, floors = read_baseline(args.baseline)
    if report_unmatched(args.pattern, measured, floors):
        return 1

    problems = []
    tests_seen = 0
    for name in sorted(measured):
        if not carries_floor(name):
            tests_seen += 1
            continue
        if not selected(name, args.pattern):
            continue
        pct = measured[name]
        floor = floors.get(name)
        if floor is None:
            problems.append(
                f"{name}: no floor recorded ({pct:.1f}% lines); add it to the baseline deliberately"
            )
        elif pct + 0.05 < floor:
            problems.append(f"{name}: {pct:.1f}% lines, below its floor of {floor:.1f}%")

    if problems:
        for problem in problems:
            print(f"{PREFIX}{problem}", file=sys.stderr)
        return 1
    print(
        f"{PREFIX}every file meets its recorded floor "
        f"({tests_seen} test file(s) reported, not compared)"
    )
    return 0


def update(args):
    measured = read_measured(args.measured)
    names, floors = read_baseline(args.baseline)
    if report_unmatched(args.pattern, measured, floors):
        return 1

    merged, notes = [], []
    for name in names:
        in_scope = selected(name, args.pattern)
        exists = os.path.exists(os.path.join(args.root, name))
        if in_scope and not exists:
            notes.append(f"dropped {name}: the file is gone")
            continue
        if name not in measured or not in_scope:
            merged.append((name, floors[name]))
            continue
        pct = measured[name]
        wanted = max(0.0, pct - args.margin)
        old = floors[name]
        if wanted < old and not args.allow_lower:
            # A run-to-run drop of a point or two is the measurement moving, not
            # a coverage decision, and a lowered floor cannot notice the loss
            # that follows. Holding costs nothing: the next run that measures
            # above the floor raises it again. A measurement that gained less
            # than the margin holds the floor too, silently: what the gate reads
            # does not change either way.
            if pct < old:
                notes.append(
                    f"held {name} at {old:.1f}% (measured {pct:.1f}%): a drop inside the"
                    " margin holds the floor; --allow-lower if it is real"
                )
            merged.append((name, old))
            continue
        if abs(wanted - old) >= 0.05:
            verb = "lowered" if wanted < old else "raised"
            notes.append(f"{verb} {name} {old:.1f} -> {wanted:.1f}%")
        merged.append((name, wanted))

    # A file with no entry is the case the gate exists for: somebody decides what
    # covers it. It lands here rather than in the comparison so a run that adds a
    # source and its tests does not have to invent the line first.
    added = 0
    for name in sorted(measured):
        if name in floors or not carries_floor(name):
            continue
        if not selected(name, args.pattern):
            continue
        if not os.path.exists(os.path.join(args.root, name)):
            continue
        floor = max(0.0, measured[name] - args.margin)
        merged.append((name, floor))
        notes.append(f"added {name} {floor:.1f}% (measured {measured[name]:.1f}%)")
        added += 1

    os.makedirs(os.path.dirname(args.baseline) or ".", exist_ok=True)
    with open(args.baseline, "w", encoding="utf-8") as fh:
        for line in HEADER:
            fh.write(line + "\n")
        for name, floor in merged:
            fh.write(f"{name} {floor:.1f}\n")

    for note in notes:
        print(f"{PREFIX}{note}")
    shown = os.path.relpath(args.baseline, args.root)
    if not notes:
        print(f"{PREFIX}no floor changed; {len(merged)} entry(s) in {shown}")
    else:
        print(f"{PREFIX}wrote {len(merged)} floor(s) to {shown} ({added} new)")
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode", required=True)

    def common(name):
        p = sub.add_parser(name)
        p.add_argument("--measured", required=True)
        p.add_argument("--baseline", required=True)
        p.add_argument("--root", required=True)
        p.add_argument("--margin", type=float, default=DEFAULT_MARGIN)
        p.add_argument("--pattern", action="append", default=[])
        return p

    common("check")
    upd = common("update")
    upd.add_argument("--allow-lower", action="store_true")

    args = parser.parse_args(argv)
    return check(args) if args.mode == "check" else update(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
