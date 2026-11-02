#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Compare the graph construction trees (GCT) two builds of the compiler make of the corpus.

    python3 test/tools/gctcheck/check.py dump OUT_DIR [--camel-home HOME] [--jobs N]
    python3 test/tools/gctcheck/check.py compare DIR_A DIR_B
    python3 test/tools/gctcheck/check.py control
    python3 test/tools/gctcheck/check.py shape DUMP_DIR

`dump` runs `camel inspect --gct` on every .cml file of the repository (tests, examples,
exemplars, benchmarks, the standard library) and writes, per file, one record to OUT_DIR. The
record says whether the run produced a tree or a refusal, so the two are compared separately:
a tree that replaced a refusal is its own kind of difference and is reported as one.

The tree is the dump format of camel/compile/gct/tree.h, and one thing in it differs from run to
run: the address printed for a constant (`DATA: 0x...`), which allocates differently on every
run. Each distinct address is replaced by its own token (`0xA1`, `0xA2`, ... in the order the
addresses first appear), never by one shared word: two occurrences of the same constant stay one
token and two different constants stay two, so a reader that copies a shared value into two
places is a difference here instead of a pass. `control` is the reading that says so -- it feeds
the comparer a record whose shared token was split in two and requires the difference.

`compare` reports the files whose record differs between two dumps and exits with 1 when there
are any.

The GCT has no recorded golden: its dump is a debugging view, and a golden over the whole
corpus would have to be re-recorded with every new test program. This tool is for the one
who changes the GCT builder or the tree's representation: dump with the build before the
change, dump with the build after it, compare.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
CORPUS_DIRS = ["test", "examples", "exemplars", "benchmarks", "stdlib"]
# Build outputs and the logs of earlier test runs (the same list as test/tools/parsecheck/corpus.py).
SKIP_DIRS = {".git", "node_modules", "build", "out", "tmp", "third_party", "dist", "vendor",
             ".cache", "__pycache__", "tmp_q", "results"}
ADDRESS = re.compile(r"0x[0-9A-Fa-f]{6,}")
DIAGNOSTIC = re.compile(r"\b[A-Za-z]+Diag::[A-Za-z]+")
KINDS = ("tree", "refusal")


def corpus() -> list:
    files = []
    for top in CORPUS_DIRS:
        for root, dirs, names in os.walk(REPO / top):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith("out_lr")]
            files += [Path(root) / n for n in names if n.endswith(".cml")]
    return sorted(files)


def record_name(path: Path) -> str:
    relative = path.relative_to(REPO).as_posix()
    return hashlib.sha1(relative.encode()).hexdigest()[:16] + ".txt"


def tokenize_addresses(text: str) -> str:
    """One token per distinct address, in order of first appearance.

    A single token for every address would hide the difference this tool exists to show: a
    graph that shares one constant has the same address twice, and a graph that copies it has
    two addresses. Masking both to `0xADDRESS` makes those two graphs compare equal.
    """
    tokens: dict[str, str] = {}

    def replace(match: re.Match) -> str:
        key = match.group(0).lower()
        if key not in tokens:
            tokens[key] = f"0xA{len(tokens) + 1}"
        return tokens[key]

    return ADDRESS.sub(replace, text)


def make_record(relative: str, exit_code: int, stdout: str, stderr: str) -> str:
    tree = exit_code == 0 and stdout.strip() != ""
    kind = "tree" if tree else "refusal"
    codes = sorted(set(DIAGNOSTIC.findall(stderr)))
    body = tokenize_addresses(stdout if tree else (stderr or stdout))
    lines = [relative, f"kind: {kind}", f"exit: {exit_code}",
             "codes:" + ("" if not codes else " " + ", ".join(codes))]
    return "\n".join(lines) + "\n" + body


def dump_one(args) -> str:
    path, home, out_dir = args
    env = dict(os.environ, CAMEL_HOME=str(home), CAMEL_PACKAGES=str(REPO / "benchmarks" / "common"))
    relative = path.relative_to(REPO).as_posix()
    try:
        done = subprocess.run([str(Path(home) / "bin" / "camel"), "inspect", "--gct", path.name],
                              cwd=str(path.parent), env=env, capture_output=True, timeout=60)
        record = make_record(relative, done.returncode,
                             done.stdout.decode("utf-8", "replace"),
                             done.stderr.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        record = make_record(relative, 124, "", "timeout")
    (out_dir / record_name(path)).write_text(record, encoding="utf-8", newline="\n")
    return relative


def cmd_dump(args) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    home = Path(args.camel_home or os.environ.get("CAMEL_HOME") or REPO / "out" / "latest").resolve()
    files = corpus()
    if not files:
        print("gctcheck: no .cml in the corpus; nothing was dumped")
        return 2
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(dump_one, [(f, home, out_dir) for f in files]))
    print(f"gctcheck: {len(done)} files dumped to {out_dir}")
    return 0


def split_record(text: str) -> dict:
    lines = text.splitlines()
    header, body = lines[:1], lines[1:]
    fields = {"path": header[0] if header else "", "kind": "", "exit": "", "codes": ""}
    while body and body[0].split(":")[0] in ("kind", "exit", "codes"):
        key, _, value = body.pop(0).partition(":")
        fields[key] = value.strip()
    fields["body"] = "\n".join(body)
    return fields


def differences(left_text: str, right_text: str) -> list:
    """The kinds of difference between two records; empty when they agree."""
    left, right = split_record(left_text), split_record(right_text)
    if left["kind"] != right["kind"]:
        return [f"{left['kind'] or 'missing'} became {right['kind'] or 'missing'}"]
    if left["exit"] != right["exit"]:
        return [f"exit {left['exit']} became {right['exit']}"]
    if left["codes"] != right["codes"]:
        return [f"diagnostics changed: [{left['codes']}] -> [{right['codes']}]"]
    if left["body"] != right["body"]:
        return ["tree text changed" if left["kind"] == "tree" else "refusal text changed"]
    return []


def parse_line(line: str):
    """(depth, is_last, label) for one dump line, or None when the indentation is not the grammar."""
    depth, i = 0, 0
    while True:
        if line.startswith("|-", i) or line.startswith("\\-", i):
            return depth, line[i] == "\\", line[i + 2:]
        if line.startswith("|  ", i):
            depth, i = depth + 1, i + 3
            continue
        if line.startswith("   ", i):
            depth, i = depth + 1, i + 3
            continue
        return (depth, False, line[i:]) if i == 0 else None


def cmd_shape(args) -> int:
    """The inventory a reader of the dump is written against: what the text carries, and how much
    of it there is. A line the indentation cannot place is a failure, because it would be a node a
    reader could not put anywhere."""
    root = Path(args.dump_dir)
    records = sorted(root.glob("*.txt"))
    if not records:
        print(f"gctcheck: no record in {root}; nothing was read")
        return 2
    trees = refusals = 0
    lines_total = depth_max = largest = 0
    labels: dict = {}
    anomalies = []
    for record in records:
        text = record.read_text(encoding="utf-8").splitlines()
        fields, body, i = {}, text[1:], 0
        while i < len(body) and body[i].split(":")[0] in ("kind", "exit", "codes"):
            key, _, value = body[i].partition(":")
            fields[key] = value.strip()
            i += 1
        if fields.get("kind") != "tree":
            refusals += 1
            continue
        trees += 1
        body = body[i:]
        lines_total += len(body)
        largest = max(largest, len(body))
        previous = -1
        for number, line in enumerate(body):
            parsed = parse_line(line)
            if parsed is None:
                anomalies.append((record.name, f"line {number + 1}: {line[:60]!r}"))
                continue
            depth, _, label = parsed
            depth_max = max(depth_max, depth)
            if depth > previous + 1:
                anomalies.append((record.name, f"line {number + 1}: depth {depth} after {previous}"))
            previous = depth
            name = label.split(":")[0].strip() or label.strip()
            labels[name] = labels.get(name, 0) + 1
    print(f"gctcheck: {len(records)} records, {trees} trees, {refusals} refusals")
    print(f"gctcheck: {lines_total} tree lines, deepest indent {depth_max}, largest tree {largest} lines")
    print(f"gctcheck: {len(labels)} node labels, {len(anomalies)} lines the grammar cannot place")
    for name, count in sorted(labels.items(), key=lambda pair: -pair[1])[:20]:
        print(f"  {count:7d}  {name}")
    for record, why in anomalies[:10]:
        print(f"ANOMALY {record}: {why}")
    return 1 if anomalies else 0


def cmd_compare(args) -> int:
    a, b = Path(args.dir_a), Path(args.dir_b)
    names = sorted({p.name for p in a.glob("*.txt")} | {p.name for p in b.glob("*.txt")})
    if not names:
        # Two empty directories (or two directories with the wrong names) agree on nothing;
        # "0 files, 0 different" is not a pass.
        print(f"gctcheck: no record in {a} or {b}; nothing was compared")
        return 2
    tree_count, refusals, different, kinds = 0, 0, [], {}
    for name in names:
        left = (a / name).read_text(encoding="utf-8") if (a / name).exists() else ""
        right = (b / name).read_text(encoding="utf-8") if (b / name).exists() else ""
        fields = split_record(left or right)
        tree_count += fields["kind"] == "tree"
        refusals += fields["kind"] == "refusal"
        found = differences(left, right) if left and right else ["missing on one side"]
        if found:
            different.append((fields["path"], found[0]))
            kinds[found[0]] = kinds.get(found[0], 0) + 1
    for path, reason in different[:50]:
        print(f"DIFFERENT {path}: {reason}")
    summary = f"gctcheck: {len(names)} files, {tree_count} with a tree, {refusals} refusals, {len(different)} different"
    if kinds:
        summary += " (" + ", ".join(f"{count} {reason}" for reason, count in sorted(kinds.items())) + ")"
    print(summary)
    return 1 if different else 0


def cmd_control(args) -> int:
    """The reading this tool needs before anyone trusts it: it must notice what it is for.

    Three records, hand-written, so the expectation does not depend on a compiler:
    the same record twice (must agree), a record whose shared token was split into two
    (must differ: a graph that copies a constant instead of sharing it), and a refusal next
    to a tree (must differ, and as its own kind).
    """
    shared = ("case.cml\nkind: tree\nexit: 0\ncodes:\n"
              "EXEC\n|-DATA: 0xA1, int, 0L\n\\-DATA: 0xA1, int, 0L\n")
    split = shared.replace("\\-DATA: 0xA1", "\\-DATA: 0xA2")
    refusal = "case.cml\nkind: refusal\nexit: 1\ncodes: SemanticDiag::NoSuchModule\nno such module\n"
    checks = [
        ("two dumps of the same record agree", shared, shared, False),
        ("a copied constant is a difference", shared, split, True),
        ("a refusal that became a tree is a difference", refusal, shared, True),
    ]
    failures = 0
    with tempfile.TemporaryDirectory() as root:
        for label, left, right, expect_difference in checks:
            a, b = Path(root, "a"), Path(root, "b")
            for directory, text in ((a, left), (b, right)):
                directory.mkdir(exist_ok=True)
                (directory / "case.txt").write_text(text, encoding="utf-8")
            found = differences(left, right)
            got = bool(found)
            ok = got == expect_difference
            failures += not ok
            reason = f" ({found[0]})" if found else ""
            print(f"{'PASS' if ok else 'FAIL'} control: {label}: "
                  f"{'different' if got else 'same'}{reason}")
    if failures:
        print(f"gctcheck: {failures} of {len(checks)} controls did not hold")
        return 1
    print(f"gctcheck: {len(checks)} controls hold")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("dump")
    p.add_argument("out_dir")
    p.add_argument("--camel-home")
    p.add_argument("--jobs", type=int, default=6)
    p.set_defaults(fn=cmd_dump)
    p = sub.add_parser("compare")
    p.add_argument("dir_a")
    p.add_argument("dir_b")
    p.set_defaults(fn=cmd_compare)
    p = sub.add_parser("control")
    p.set_defaults(fn=cmd_control)
    p = sub.add_parser("shape")
    p.add_argument("dump_dir")
    p.set_defaults(fn=cmd_shape)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())