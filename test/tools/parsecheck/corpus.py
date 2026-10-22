#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Assembles the corpus the parser harness (camel-parsecheck) runs on.

    python3 test/tools/parsecheck/corpus.py --out DIR [--scope parse|repository]
                                            [--progen N] [--progen-big M]

Writes DIR/list.txt, one source path per line:

  * every .cml file of the parse corpus, test/cases/parse/ (the cases that own the parser), or,
    with --scope repository, every .cml file of the repository (tests, benchmarks, exemplars,
    examples, the standard library, the tutorial), the syntax-negative cases included. The
    snapshot that check.py compares (test/plans/feat/parse/golden/ast_hashes.txt) is the parse
    corpus: a snapshot of the parser's tests, not of the repository (H188). The repository scope
    is what check.py's format and fuzz checks traverse;
  * every ```cml block of the Markdown documents, extracted to DIR/docs/;
  * N programs of the random generator test/tools/progen (seeds 1..N) and M at --size 4,
    written to DIR/progen/.

The documents and progen groups belong to the differential harness this tool was written for;
they are still built on request, but nothing in the tree consumes them any more: check.py builds
the snapshot list from the .cml groups alone.


Run from the repository root.
"""
import argparse
import os
import re
import sys
from pathlib import Path

SKIP_DIRS = {".git", "node_modules", "build", "out", "tmp", "third_party", "dist", "vendor",
             ".cache", "__pycache__", "tmp_q", "results"}

PARSE_ROOT = Path("test/cases/parse")


def walk(root: Path, suffix: str):
    for base, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("out_lr"))
        for name in sorted(files):
            if name.endswith(suffix):
                yield Path(base) / name


def parse_cases():
    """The parse corpus: every .cml case under test/cases/parse/. The AST snapshot hashes these
    and the probes: the snapshot is a snapshot of the tests that own the parser (H188)."""
    return walk(PARSE_ROOT, ".cml")


def doc_blocks(out: Path):
    fence = re.compile(r"^(\s*)```(cml|camel)\s*$")
    written = []
    for md in walk(Path("."), ".md"):
        lines = md.read_text(encoding="utf-8", errors="replace").split("\n")
        block, indent, count = None, "", 0
        for line in lines:
            if block is None:
                m = fence.match(line)
                if m:
                    block, indent = [], m.group(1)
            elif line.strip() == "```":
                count += 1
                slug = re.sub(r"[^A-Za-z0-9]+", "_", str(md.with_suffix("")).lstrip("./"))
                path = out / "docs" / f"{slug}_{count:03d}.cml"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("\n".join(block) + "\n", encoding="utf-8", newline="\n")
                written.append(path)
                block = None
            else:
                block.append(line[len(indent):] if line.startswith(indent) else line)
    return written


def progen(out: Path, count: int, size: int):
    if count <= 0:
        return []
    sys.path.insert(0, str(Path("test/tools/progen").resolve()))
    from generate import generate, DEFAULT_FEATURES  # noqa: E402
    from model import emit  # noqa: E402
    written = []
    folder = out / ("progen" if size == 1 else f"progen_size{size}")
    folder.mkdir(parents=True, exist_ok=True)
    for seed in range(1, count + 1):
        prog, _, _ = generate(seed, DEFAULT_FEATURES, size=size)
        path = folder / f"seed_{seed:05d}.cml"
        path.write_text(emit(prog) + "\n", encoding="utf-8", newline="\n")
        written.append(path)
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument(
        "--scope",
        choices=["parse", "repository"],
        default="parse",
        help="the .cml files of list.txt: the parse corpus test/cases/parse (the default) or every"
             " .cml file of the repository",
    )
    ap.add_argument("--progen", type=int, default=0)
    ap.add_argument("--progen-big", type=int, default=0)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if args.scope == "parse":
        cml = list(parse_cases())
    else:
        cml = list(walk(Path("."), ".cml"))
    groups = {
        args.scope: cml,
        "documents": doc_blocks(out),
        "progen": progen(out, args.progen, 1),
        "progen size 4": progen(out, args.progen_big, 4),
    }
    with open(out / "list.txt", "w", encoding="utf-8", newline="\n") as f:
        for files in groups.values():
            for p in files:
                f.write(str(p).replace("\\", "/") + "\n")
    for name, files in groups.items():
        print(f"{name}: {len(files)}")
    print("total:", sum(len(v) for v in groups.values()))


if __name__ == "__main__":
    main()
