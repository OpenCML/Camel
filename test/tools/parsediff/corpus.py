#!/usr/bin/env python3
"""Assembles the corpus the parser harness (camel-parsediff) runs on.

    python3 test/tools/parsediff/corpus.py --out DIR [--progen N] [--progen-big M]

Writes DIR/list.txt, one source path per line:

  * every .cml file of the repository (tests, benchmarks, exemplars, examples, the standard
    library, the tutorial), the syntax-negative cases included;
  * every ```cml block of the Markdown documents, extracted to DIR/docs/;
  * N programs of the random generator test/tools/progen (seeds 1..N) and M at --size 4,
    written to DIR/progen/.

Run from the repository root.
"""
import argparse
import os
import re
import sys
from pathlib import Path

SKIP_DIRS = {".git", "node_modules", "build", "out", "tmp", "third_party", "dist", "vendor",
             ".cache", "__pycache__", "tmp_q", "results"}


def walk(root: Path, suffix: str):
    for base, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("out_lr"))
        for name in sorted(files):
            if name.endswith(suffix):
                yield Path(base) / name


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
    ap.add_argument("--progen", type=int, default=0)
    ap.add_argument("--progen-big", type=int, default=0)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    groups = {
        "repository": [p for p in walk(Path("."), ".cml")],
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
