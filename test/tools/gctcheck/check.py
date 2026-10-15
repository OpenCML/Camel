#!/usr/bin/env python3
"""Compare the graph construction trees (GCT) two builds of the compiler make of the corpus.

    python3 test/tools/gctcheck/check.py dump OUT_DIR [--camel-home HOME] [--jobs N]
    python3 test/tools/gctcheck/check.py compare DIR_A DIR_B

`dump` runs `camel inspect --gct` on every .cml file of the repository (tests, examples,
exemplars, benchmarks, the standard library) and writes, per file, the exit code and the
printed tree to OUT_DIR. The tree is the dump format of camel/compile/gct/tree.h; the one
thing in it that differs from run to run, the address printed for a constant
(`DATA: 0x...`), is replaced by a fixed word. Files that do not compile are kept too: their
diagnostics are part of what must not change.

`compare` reports the files whose record differs between two dumps and exits with 1 when
there are any.

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
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
CORPUS_DIRS = ["test", "examples", "exemplars", "benchmarks", "stdlib"]
# Build outputs and the logs of earlier test runs (the same list as test/tools/parsecheck/corpus.py).
SKIP_DIRS = {".git", "node_modules", "build", "out", "tmp", "third_party", "dist", "vendor",
             ".cache", "__pycache__", "tmp_q", "results"}
ADDRESS = re.compile(r"0x[0-9A-Fa-f]{6,}")


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


def dump_one(args) -> str:
    path, home, out_dir = args
    env = dict(os.environ, CAMEL_HOME=str(home), CAMEL_PACKAGES=str(REPO / "benchmarks" / "common"))
    try:
        done = subprocess.run([str(Path(home) / "bin" / "camel"), "inspect", "--gct", path.name],
                              cwd=str(path.parent), env=env, capture_output=True, timeout=60)
        text = f"exit {done.returncode}\n" + ADDRESS.sub("0xADDRESS", done.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        text = "timeout\n"
    relative = path.relative_to(REPO).as_posix()
    (out_dir / record_name(path)).write_text(relative + "\n" + text, encoding="utf-8", newline="\n")
    return relative


def cmd_dump(args) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    home = Path(args.camel_home or os.environ.get("CAMEL_HOME") or REPO / "out" / "latest").resolve()
    files = corpus()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        done = list(pool.map(dump_one, [(f, home, out_dir) for f in files]))
    print(f"gctcheck: {len(done)} files dumped to {out_dir}")
    return 0


def cmd_compare(args) -> int:
    a, b = Path(args.dir_a), Path(args.dir_b)
    names = sorted({p.name for p in a.glob("*.txt")} | {p.name for p in b.glob("*.txt")})
    different, trees = [], 0
    for name in names:
        left = (a / name).read_text(encoding="utf-8") if (a / name).exists() else ""
        right = (b / name).read_text(encoding="utf-8") if (b / name).exists() else ""
        trees += "\nexit 0\n" in left and len(left.splitlines()) > 2
        if left != right:
            different.append((left or right).splitlines()[0])
    for relative in different[:50]:
        print(f"DIFFERENT {relative}")
    print(f"gctcheck: {len(names)} files, {trees} with a tree, {len(different)} different")
    return 1 if different else 0


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
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
