#!/usr/bin/env python3
"""Generate a multi-module Camel project for the compile benchmark.

    python3 benchmarks/compile/gen_modules.py OUT_DIR --modules 64 --shape chain --funcs 8

writes OUT_DIR/main.cml and OUT_DIR/bm_0000.cml ... (N module files). `main` imports one
function (`entry_NNNN`) of every module, calls each once and prints the sum, so the expected output is known
without running Camel: it is printed by this script as the last line of standard output and
written to OUT_DIR/expected.txt.

Shapes (what a module imports besides being imported by main):

  flat    nothing: N independent leaves. The cost per module of finding, parsing and compiling
          a file, and of importing its names into main.
  chain   module i imports module i-1 and calls into it: imports nest N deep, and every
          module after the first resolves one name of an already compiled module.
  fan     module i imports the K modules before it (--fan K, default 4): each module is
          asked for again by several later ones and must come from the cache.

Every module has --funcs exported functions of a few statements each (arithmetic, an `if`,
a call of the function before it), so that the modules are small but not empty.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List

SHAPES = ("flat", "chain", "fan")


def module_name(index: int) -> str:
    return f"bm_{index:04d}"


def imports_of(index: int, shape: str, fan: int) -> List[int]:
    """Indices of the modules that module `index` imports."""
    if shape == "flat" or index == 0:
        return []
    if shape == "chain":
        return [index - 1]
    return list(range(max(0, index - fan), index))


def value_of(index: int, shape: str, fan: int, funcs: int, memo: dict) -> int:
    """What `entry_NNNN(1)` of module `index` returns: the reference for the program's output."""
    if index in memo:
        return memo[index]
    x = 1
    acc = 0
    for k in range(funcs):
        # func f_k(x) = (if x % 2 == 0 then x * 3 else x + (index % 7) + k) + f_{k-1}(x)
        acc = (x * 3 if x % 2 == 0 else x + (index % 7) + k) + acc
    deps = imports_of(index, shape, fan)
    if deps:
        # Reduced modulo 97, so that the result stays small however deep the imports nest.
        acc += sum(value_of(dep, shape, fan, funcs, memo) for dep in deps) % 97
    memo[index] = acc
    return acc


def module_source(index: int, shape: str, fan: int, funcs: int) -> str:
    name = module_name(index)
    deps = imports_of(index, shape, fan)
    lines = [f"module {name}", ""]
    for dep in deps:
        lines.append(f"import {{ entry_{dep:04d} }} from {module_name(dep)}")
    if deps:
        lines.append("")
    for k in range(funcs):
        lines.append(f"export func f_{k}(x: int): int {{")
        lines.append(f"    let a = if x % 2 == 0 then x * 3 else x + {index % 7} + {k}")
        lines.append(f"    return a + f_{k - 1}(x)" if k > 0 else "    return a")
        lines.append("}")
        lines.append("")
    lines.append(f"export func entry_{index:04d}(x: int): int {{")
    if deps:
        imported = " + ".join(f"entry_{dep:04d}(x)" for dep in deps)
        lines.append(f"    return f_{funcs - 1}(x) + ({imported}) % 97")
    else:
        lines.append(f"    return f_{funcs - 1}(x)")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main_source(modules: int) -> str:
    lines = ["module main", ""]
    for index in range(modules):
        lines.append(f"import {{ entry_{index:04d} }} from {module_name(index)}")
    lines.append("")
    lines.append("func main(): int sync {")
    lines.append("    var total = 0")
    for index in range(modules):
        lines.append(f"    total = total + entry_{index:04d}(1)")
    lines.append("    println(total)")
    lines.append("    return 0")
    lines.append("}")
    return "\n".join(lines) + "\n"


def generate(out_dir: Path, modules: int, shape: str, funcs: int, fan: int) -> int:
    """Writes the project and returns the number main prints."""
    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in out_dir.glob("bm_*.cml"):
        stale.unlink()
    memo: dict = {}
    total = 0
    for index in range(modules):
        (out_dir / f"{module_name(index)}.cml").write_text(
            module_source(index, shape, fan, funcs), encoding="utf-8", newline="\n")
        total += value_of(index, shape, fan, funcs, memo)
    (out_dir / "main.cml").write_text(main_source(modules), encoding="utf-8", newline="\n")
    (out_dir / "expected.txt").write_text(f"{total}\n", encoding="utf-8", newline="\n")
    return total


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--modules", type=int, required=True, help="number of imported modules")
    ap.add_argument("--shape", choices=SHAPES, default="flat")
    ap.add_argument("--funcs", type=int, default=8, help="exported functions per module")
    ap.add_argument("--fan", type=int, default=4, help="imports per module in the fan shape")
    args = ap.parse_args(argv)
    if args.modules < 1 or args.funcs < 1 or args.fan < 1:
        ap.error("--modules, --funcs and --fan must be positive")
    sys.setrecursionlimit(max(1000, args.modules * 4))
    print(generate(args.out_dir, args.modules, args.shape, args.funcs, args.fan))
    return 0


if __name__ == "__main__":
    sys.exit(main())
