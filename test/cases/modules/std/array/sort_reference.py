#!/usr/bin/env python3
"""The `sort` of the array module against Python's sorted(), on generated arrays (fixed seed).

Arrays of int and of float; random, with duplicates, already sorted, reversed, constant; lengths 1 to
257. `sort` (which returns a new array and leaves the input unchanged, design principles 11.3) is
applied to each array, also as the in-place rebinding `v = v->sort` (11.3). A second program sorts
200000 elements that are already sorted, and reversed.

Why this exists: the sorting operators once returned unsorted arrays (a wrong partition) and no test
compared their output with anything.
"""
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[4]
HOME = Path(os.environ.get("CAMEL_HOME", REPO_ROOT / "out" / "latest"))
CAMEL = HOME / "bin" / ("camel.exe" if os.name == "nt" else "camel")
# `sort` returns a new array and leaves its argument unchanged (design principles 11.3). It is
# also checked as the rebinding `v = v->sort` of a variable, which the compiler turns into the
# kernel's in-place form (Builder::inPlaceRebindingOf): on a copy the first time (the variable was
# made from a value something else refers to), on the variable's own array the second time.
COPYING = ["sort"]


def lit(xs, is_float):
    return "[" + ", ".join((f"{x:.3f}" if is_float else str(x)) for x in xs) + "]"


def arrays(rng):
    out = []
    for n in [1, 2, 3, 8, 15, 16, 17, 64, 257]:
        for kind in ["random", "dups", "sorted", "reversed", "constant"]:
            for is_float in (False, True):
                if kind == "constant":
                    xs = [rng.randint(-5, 5)] * n
                elif kind == "dups":
                    xs = [rng.randint(-3, 3) for _ in range(n)]
                else:
                    xs = [rng.randint(-1000, 1000) for _ in range(n)]
                    if kind == "sorted":
                        xs.sort()
                    if kind == "reversed":
                        xs.sort(reverse=True)
                if is_float:
                    xs = [x / 8 for x in xs]
                out.append((xs, is_float))
    return out


def numbers(line):
    return [float(t) for t in re.findall(r"-?\d+(?:\.\d+)?(?:e-?\d+)?", line)]


def run(path, cwd):
    proc = subprocess.run([str(CAMEL), str(path)], cwd=cwd, env={**os.environ, "CAMEL_HOME": str(HOME)},
                          capture_output=True, text=True, timeout=300)
    return proc.returncode, proc.stdout.splitlines(), proc.stderr


def main() -> int:
    rng = random.Random(20261009)
    body, expected = [], []
    k = 0
    for xs, is_float in arrays(rng):
        ref = sorted(xs)
        for op in COPYING:
            k += 1
            body += [f"    let a{k} = {lit(xs, is_float)}", f"    let r{k} = {op}(a{k})",
                     f"    println('#', r{k})", f"    println('#', a{k})",
                     f"    var v{k} = a{k}", f"    v{k} = v{k}->{op}", f"    println('#', v{k})",
                     f"    println('#', a{k})", f"    v{k} = v{k}->{op}", f"    println('#', v{k})"]
            expected += [ref, xs, ref, xs, ref]
    names = ", ".join(COPYING)
    small = f"module array_reference\nimport {{ {names} }} from array\n\nfunc main(): int sync {{\n" + "\n".join(body) + "\n    return 0\n}\n"
    n = 200000
    large = f"""module array_large
import {{ sort }} from array

func main(): int sync {{
    let up = 0..{n}
    let down = up->map<(i: int): int => {n} - 1 - i>
    let a = sort(up)
    var b = down
    b = b->sort
    let c = sort(down)
    let d = sort(up->map<(i: int): int => (i * 7919) % {n}>)
    println('#', a[0], a[{n // 2}], a[{n - 1}], b[0], b[{n // 2}], b[{n - 1}], c[0], c[{n // 2}], c[{n - 1}], d[0], d[{n // 2}], d[{n - 1}])
    return 0
}}
"""
    wrong = 0
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "small.cml").write_text(small, encoding="utf-8")
        (Path(tmp) / "large.cml").write_text(large, encoding="utf-8")
        code, lines, err = run(Path(tmp) / "small.cml", tmp)
        got = [numbers(line[1:]) for line in lines if line.startswith("#")]
        if code != 0 or len(got) != len(expected):
            print(f"small program: exit {code}, {len(got)} of {len(expected)} lines; {err.strip()[:300]} {' '.join(lines[:2])[:300]}")
            wrong += 1
        else:
            for index, (g, e) in enumerate(zip(got, expected)):
                if g != [float(x) for x in e]:
                    wrong += 1
                    if wrong <= 5:
                        print(f"line {index}: got {g[:12]} expected {e[:12]}")
        code, lines, err = run(Path(tmp) / "large.cml", tmp)
        got = [numbers(line[1:]) for line in lines if line.startswith("#")]
        want = [float(x) for x in [0, n // 2, n - 1] * 4]
        if code != 0 or got != [want]:
            print(f"large program: exit {code}; got {got}; {err.strip()[:300]}")
            wrong += 1
    print(f"array sorting reference: seed 20261009, {len(expected)} results and one large program, {wrong} wrong")
    print("PASS" if wrong == 0 else "FAIL")
    return 0 if wrong == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
