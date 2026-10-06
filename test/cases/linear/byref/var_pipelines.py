"""Generated programs that write variables, compared across pipelines and with a reference.\n\nEach program declares recursive `sync` functions with a `var` parameter that write the variable on\nthe way down and / or up (all writes declared and waited), calls them from `main` at several\ndepths, and prints the results and the final values of the variables. The expected output is\ncomputed here by a direct simulation, so every pipeline is compared with the reference and not\nonly with the default run. Depths go beyond the number of inlining rounds after which a lost write\nfirst appeared (4).\n\nStraight-line functions add the other shape: snapshots of the variable (`let k = c + 0`) interleaved\nwith writes, a snapshot feeding an argument that the callee ignores while the callee writes the\nvariable, a recursion between two snapshots, and a branch whose arms write.\n\nWhy this exists: std::opt::simplify once dropped a declared write to a var (one increment missing)\nin a recursion whose last call returned a constant, and once let a read of a var slip behind later\nwrites (the read fed an argument the inlined callee did not use). The first version of this\ngenerator had only recursions and passed while the second defect existed.\n\nUsage: python var_pipelines.py [--seed N] [--programs N]\nThe compiler is REPO_ROOT/out/latest unless CAMEL_EXE / CAMEL_HOME are set.\n"""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]

PIPELINES = [
    [],
    ["std::fvm"],
    ["std::opt::simplify"],
    ["std::macro", "std::opt::simplify", "std::fvm"],
    ["std::macro", "std::opt::simplify", "std::jit"],
    ["std::inline", "std::inline", "std::inline", "std::inline", "std::inline"],
    ["std::opt", "std::opt::fold", "std::opt", "std::opt::fold", "std::opt", "std::opt::fold", "std::opt", "std::opt::fold", "std::opt::dce"],
    ["std::opt::simplify", "std::tf"],
]


def make_function(rng: random.Random, j: int):
    pre = rng.choice([0, 1, 2, 10])
    post = rng.choice([0, 0, 1, 3])
    if pre == 0 and post == 0:
        pre = 1
    base = rng.choice([0, 7, -1])          # what the last call returns: a constant
    bump = rng.choice([0, 0, 1])           # what each level adds to the result on the way back
    lines = [f"sync func rec{j}(i: int, n: int, var c: int): int sync {{", "    return if i >= n then " + str(base) + " else sync {"]
    if pre:
        lines.append(f"        c += {pre}")
    lines.append(f"        let r = wait rec{j}(i + 1, n, c)")
    if post:
        lines.append(f"        c += {post}")
    lines.append("        r + " + str(bump) if bump else "        r")
    lines += ["    }", "}", ""]

    def simulate(n: int, c: int):
        return base + bump * n, c + (pre + post) * n

    return "\n".join(lines), simulate


def make_straight(rng: random.Random, j: int, rec_sims):
    """Straight-line code: snapshots of the variable interleaved with writes; a snapshot feeds an\n    argument the callee ignores (the callee writes the variable), and a branch whose arms write."""
    wa, wb = rng.choice([1, 2, 5]), rng.choice([0, 3])
    a1, a2 = rng.choice([1, 2, 4]), rng.choice([1, 3])
    t, ba, bb = rng.choice([1, 2, 3]), rng.choice([1, 10]), rng.choice([2, 20])
    use_rec = rng.random() < 0.6
    rj, depth = rng.randrange(len(rec_sims)), rng.randint(1, 5)
    use_branch = rng.random() < 0.6
    lines = [f"sync func ign{j}(x: int, var c: int): int sync {{", f"    c += {wa}", f"    return {wb}", "}", "",
             f"sync func line{j}(var c: int): int sync {{", f"    c += {a1}", "    let k1 = c + 0",
             f"    let r1 = wait ign{j}(k1 * 3, c)", "    let k2 = c + 0", f"    c += {a2}"]
    if use_rec:
        lines.append(f"    let r2 = wait rec{rj}(0, {depth}, c)")
    if use_branch:
        lines.append(f"    let r3 = if k1 > {t} then sync {{ c += {ba}; 1 }} else sync {{ c += {bb}; 2 }}")
    lines.append("    let k3 = c + 0")
    tail = "k1 * 1000000 + k2 * 10000 + k3 * 100 + r1" + (" + r2" if use_rec else "") + (" + r3" if use_branch else "")
    lines += [f"    return {tail}", "}", ""]

    def simulate(c: int):
        c += a1; k1 = c
        c += wa; r1 = wb
        k2 = c
        c += a2
        r2 = 0
        if use_rec:
            r2, c = rec_sims[rj](depth, c)
        r3 = 0
        if use_branch:
            if k1 > t:
                c += ba; r3 = 1
            else:
                c += bb; r3 = 2
        k3 = c
        return k1 * 1000000 + k2 * 10000 + k3 * 100 + r1 + r2 + r3, c

    return "\n".join(lines), simulate


def make_program(rng: random.Random, k: int):
    functions, sims = [], []
    for j in range(3):
        text, sim = make_function(rng, j)
        functions.append(text)
        sims.append(sim)
    body, expected = [], []
    v = 0
    for j in range(3):
        for depth in sorted(rng.sample(range(0, 8), 3)):
            v += 1
            start = rng.choice([0, 5])
            body.append(f"    var v{v} = {start}")
            body.append(f"    let x{v} = wait rec{j}(0, {depth}, v{v})")
            body.append(f"    println('f{j} depth {depth}: {{}} {{}}'->format<x{v}, v{v}>)")
            r, c = sims[j](depth, start)
            expected.append(f"f{j} depth {depth}: {r} {c}")
    for j in range(3):
        text, sim = make_straight(rng, j, sims)
        functions.append(text)
        for start in (0, 3):
            v += 1
            body.append(f"    var v{v} = {start}")
            body.append(f"    let x{v} = wait line{j}(v{v})")
            body.append(f"    println('line{j} from {start}: {{}} {{}}'->format<x{v}, v{v}>)")
            r, c = sim(start)
            expected.append(f"line{j} from {start}: {r} {c}")
    source = f"module var_pipelines_{k}\n\n" + "\n".join(functions) + "func main(): int sync {\n" + "\n".join(body) + "\n    return 0\n}\n"
    return source, expected


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=20261008)
    ap.add_argument("--programs", type=int, default=8)
    ap.add_argument("--keep")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    home = os.environ.get("CAMEL_HOME") or str(REPO_ROOT / "out" / "latest")
    exe = os.environ.get("CAMEL_EXE") or str(Path(home) / "bin" / ("camel.exe" if os.name == "nt" else "camel"))
    bad = cells = 0
    with tempfile.TemporaryDirectory() as t:
        work = Path(args.keep or t)
        work.mkdir(parents=True, exist_ok=True)
        for k in range(args.programs):
            source, expected = make_program(rng, k)
            (work / f"p{k}.cml").write_text(source, encoding="utf-8")
            for passes in PIPELINES:
                cells += 1
                proc = subprocess.run([exe, f"p{k}.cml", *passes], cwd=work, env={**os.environ, "CAMEL_HOME": home},
                                      capture_output=True, text=True, timeout=120)
                got = [line for line in proc.stdout.splitlines() if line.strip()]
                if proc.returncode != 0 or got != expected:
                    bad += 1
                    diff = [f"{g!r} (expected {e!r})" for g, e in zip(got, expected) if g != e][:3]
                    print(f"program {k} [{' '.join(passes) or 'default'}]: exit {proc.returncode}; "
                          f"{len(got)} of {len(expected)} lines; first differences: {diff}; stderr: {proc.stderr[-200:]!r}")
    print(f"var pipelines: seed {args.seed}, {cells} program-pipeline cells, {bad} wrong")
    print("PASS" if bad == 0 and cells > 0 else "FAIL")
    return 0 if bad == 0 and cells > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
