"""Generated programs that write variables, compared across pipelines and with a reference.

Each program declares recursive `sync` functions with a `var` parameter that write the variable on
the way down and / or up (all writes declared, in sync bodies), calls them from `main` at several
depths, and prints the results and the final values of the variables. The expected output is
computed here by a direct simulation, so every pipeline is compared with the reference and not
only with the default run. Depths go beyond the number of inlining rounds after which a lost write
first appeared (4).

Straight-line functions add the other shape: snapshots of the variable (`let k = c + 0`) interleaved
with writes, a snapshot feeding an argument that the callee ignores while the callee writes the
variable, a recursion between two snapshots, and a branch whose arms write.

Why this exists: std::opt once dropped a declared write to a var (one increment missing)
in a recursion whose last call returned a constant, and once let a read of a var slip behind later
writes (the read fed an argument the inlined callee did not use). The first version of this
generator had only recursions and passed while the second defect existed.

Nested branches add the third shape (`arms<j>`): one to three nested `if`s whose tests are only
known when the program runs (an integer read from the environment), where the innermost arm calls
a self-tail-recursive function with literal bounds (so the test of the unrolled recursion becomes
known and its branch is removed inside the arm of the run-time branch) or with a run-time bound.
The arm that ends the recursion, the sibling arms of the run-time branches and the calling arm
itself print and / or write the variable, varied by the seed, as is the arm taken. The execution
order once laid out part of one arm, then the sibling arm, then the rest (the JOIN waited on nodes
inside an arm after an inner branch was removed): under std::opt an arm ran into its
sibling (a line printed twice on NodeVM and Taskflow; a wrong number on FastVM and the JIT), exit 0.
These functions are in programs of their own (a<k>.cml beside p<k>.cml) and draw from their own
random stream, so the programs of the first two shapes are the same as before they were added.

Usage: python var_pipelines.py [--seed N] [--programs N] [--keep DIR] [--warnings] [--camel PATH]
The compiler is the install `--camel <path>` names, else this tree's REPO_ROOT/out/latest.
"""

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

sys.path.insert(0, str(REPO_ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

# The install this case measures: `--camel <path>` names it, this tree's `out/latest/bin/camel` without it (one rule, in one place: test/tools/camel_arg.py).
# `CAMEL_ARGV` is this process's arguments without the `--camel` pair.
CAMEL, CAMEL_ARGV = camel_from_argv(REPO_ROOT)

PIPELINES = [
    [],
    ["std::fvm"],
    ["std::opt"],
    ["std::macro", "std::opt", "std::fvm"],
    ["std::macro", "std::opt", "std::jit"],
    ["std::opt::inline", "std::opt::inline", "std::opt::inline", "std::opt::inline", "std::opt::inline"],
    ["std::opt", "std::opt::fold", "std::opt", "std::opt::fold", "std::opt", "std::opt::fold", "std::opt", "std::opt::fold", "std::opt::dce"],
    ["std::opt", "std::tf"],
    ["std::opt", "std::nvm"],
    ["std::opt", "std::fvm"],
    ["std::opt", "std::jit"],
]

# The run-time integer the programs read (`int(get_env(...))`): no pass knows it.
ENV_NAME, ENV_VALUE = "VAR_PIPELINES_K", 3
ARMS_FUNCTIONS = 2   # per program of the third shape


def make_function(rng: random.Random, j: int):
    pre = rng.choice([0, 1, 2, 10])
    post = rng.choice([0, 0, 1, 3])
    if pre == 0 and post == 0:
        pre = 1
    base = rng.choice([0, 7, -1])          # what the last call returns: a constant
    bump = rng.choice([0, 0, 1])           # what each level adds to the result on the way back
    lines = [f"func rec{j}(i: int, n: int, var c: int): int sync {{", "    return if i >= n then " + str(base) + " else sync {"]
    if pre:
        lines.append(f"        c += {pre}")
    lines.append(f"        let r = rec{j}(i + 1, n, c)")
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
    lines = [f"func ign{j}(x: int, var c: int): int sync {{", f"    c += {wa}", f"    return {wb}", "}", "",
             f"func line{j}(var c: int): int sync {{", f"    c += {a1}", "    let k1 = c + 0",
             f"    let r1 = ign{j}(k1 * 3, c)", "    let k2 = c + 0", f"    c += {a2}"]
    if use_rec:
        lines.append(f"    let r2 = rec{rj}(0, {depth}, c)")
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


def make_arms(rng: random.Random, j: int):
    """Nested run-time branches around a call of a self-tail-recursive function (see the module text).
    Returns the source and simulate(k, x, c) -> (result, c, printed lines)."""
    var_flavor = rng.random() < 0.3       # the recursion writes the variable (a sync body)
    mul, step = rng.choice([2, 3]), rng.choice([1, 5])
    base = rng.choice(["tap", "tap", "mul", "acc"])                # pure flavor: what ends the recursion
    base_write, level_write, base_print = rng.choice([0, 7]), rng.choice([0, 1]), rng.random() < 0.5
    i0 = rng.choice([0, 0, 1])
    run_bound = rng.random() < 0.25                                # the bound: literal, or `k - d`
    span, d = rng.choice([0, 1, 2, 2, 3, 3]), rng.choice([1, 2])
    bound = f"k - {d}" if run_bound else str(i0 + span)
    pre_print, post_print = rng.random() < 0.25, rng.random() < 0.4
    depth = rng.randint(1, 3)
    levels = []                                                    # outermost first
    for _ in range(depth):
        # `k > t` with k = 3 in the first call: the arm towards the recursion is taken 3 times in 4
        deep_then, towards = rng.random() < 0.6, rng.random() < 0.75
        levels.append({"t": rng.choice([1, 2] if towards == deep_then else [4, 5]), "deep_then": deep_then,
                       "side": rng.choice(["val", "print", "write", "both"]), "q": rng.choice([0, 4]),
                       "w": rng.choice([10, 100])})

    if var_flavor:
        ends = [f"println('basev{j} {{}}'->format<acc>)"] if base_print else []
        ends += [f"c += {base_write}"] if base_write else []
        ends.append(f"acc * {mul}")
        lines = [f"func loop{j}(i: int, n: int, acc: int, var c: int): int sync {{",
                 "    return if i >= n then sync { " + "; ".join(ends) + " } else sync {"]
        if level_write:
            lines.append(f"        c += {level_write}")
        lines += [f"        let a2 = acc + {step}", f"        loop{j}(i + 1, n, a2, c)", "    }", "}", ""]
        call = f"loop{j}({i0}, {bound}, x, c)"
    else:
        end = {"tap": f"tap{j}(acc)", "mul": f"acc * {mul}", "acc": "acc"}[base]
        lines = [f"func tap{j}(a: int): int sync {{", f"    println('tap{j} {{}}'->format<a>)", f"    return a * {mul}", "}", "",
                 f"func loop{j}(i: int, n: int, acc: int): int {{", f"    return if i >= n then {end} else {{",
                 f"        let a2 = acc + {step}", f"        loop{j}(i + 1, n, a2)", "    }", "}", ""]
        call = f"loop{j}({i0}, {bound}, x)"

    inner = ([f"println('in{j}')"] if pre_print else []) + [f"let a = {call}"]
    inner += [f"println('out{j} {{}}'->format<a>)"] if post_print else []
    effects = var_flavor or pre_print or post_print
    text = ("sync " if effects else "") + "{\n" + "\n".join(inner + ["a"]) + "\n}"
    for depth_index in range(depth - 1, -1, -1):
        lv = levels[depth_index]
        acts = ([f"println('side{j}.{depth_index}')"] if lv["side"] in ("print", "both") else []) + \
               ([f"c += {lv['w']}"] if lv["side"] in ("write", "both") else [])
        value = f"x + {lv['q']}" if lv["q"] else "x"
        side = "sync { " + "; ".join(acts + [value]) + " }" if acts else value
        effects = effects or bool(acts)
        branch = f"if k > {lv['t']} then " + (f"{text} else {side}" if lv["deep_then"] else f"{side} else {text}")
        if depth_index:
            text = ("sync " if effects else "") + "{\n" + f"let q{depth_index} = {branch}\nq{depth_index}\n" + "}"
        else:
            text = f"let r = {branch}"
    lines += [f"func arms{j}(k: int, x: int, var c: int): int sync {{", text, "    return r", "}", ""]

    def simulate(k: int, x: int, c: int):
        out = []
        for depth_index, lv in enumerate(levels):
            if (k > lv["t"]) != lv["deep_then"]:                   # the sibling arm is taken
                if lv["side"] in ("print", "both"):
                    out.append(f"side{j}.{depth_index}")
                if lv["side"] in ("write", "both"):
                    c += lv["w"]
                return x + lv["q"], c, out
        if pre_print:
            out.append(f"in{j}")
        turns = max(0, (k - d if run_bound else i0 + span) - i0)
        acc = x + step * turns
        if var_flavor:
            c += level_write * turns + base_write
            if base_print:
                out.append(f"basev{j} {acc}")
            a = acc * mul
        else:
            if base == "tap":
                out.append(f"tap{j} {acc}")
            a = acc * mul if base in ("tap", "mul") else acc
        if post_print:
            out.append(f"out{j} {a}")
        return a, c, out

    return "\n".join(lines), simulate


def make_arms_program(rng: random.Random, k: int):
    """A program of its own for the third shape: each function is called twice, the second time
    with a larger run-time integer (another arm may be taken) and a run-time or a literal `x`."""
    functions, body, expected = [], [f"    let k = int(get_env('{ENV_NAME}') ?? '')"], []
    v = 0
    for j in range(ARMS_FUNCTIONS):
        text, sim = make_arms(rng, j)
        functions.append(text)
        for dk in (0, rng.choice([1, 2, 3])):
            v += 1
            start, runtime_x = rng.choice([0, 5]), dk == 0 or rng.random() < 0.5
            x_text, x = ("k * 10 + 7", ENV_VALUE * 10 + 7) if runtime_x else ("9", 9)
            body.append(f"    var v{v} = {start}")
            body.append(f"    let x{v} = arms{j}(k + {dk}, {x_text}, v{v})")
            body.append(f"    println('arms{j} +{dk}: {{}} {{}}'->format<x{v}, v{v}>)")
            r, c, printed = sim(ENV_VALUE + dk, x, start)
            expected += printed + [f"arms{j} +{dk}: {r} {c}"]
    source = f"module var_pipelines_arms_{k}\n\nimport {{ get_env }} from env\n\n" + "\n".join(functions) + "func main(): int sync {\n" + "\n".join(body) + "\n    return 0\n}\n"
    return source, expected


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
            body.append(f"    let x{v} = rec{j}(0, {depth}, v{v})")
            body.append(f"    println('f{j} depth {depth}: {{}} {{}}'->format<x{v}, v{v}>)")
            r, c = sims[j](depth, start)
            expected.append(f"f{j} depth {depth}: {r} {c}")
    for j in range(3):
        text, sim = make_straight(rng, j, sims)
        functions.append(text)
        for start in (0, 3):
            v += 1
            body.append(f"    var v{v} = {start}")
            body.append(f"    let x{v} = line{j}(v{v})")
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
    ap.add_argument("--warnings", action="store_true", help="also show compiler warnings of correct cells")
    args = ap.parse_args(CAMEL_ARGV)
    rng = random.Random(args.seed)
    # The install this run measures: `--camel <path>` names it; without it this tree's
    # `out/latest`. One rule, in one place: test/tools/camel_arg.py.
    home = str(Path(CAMEL).resolve().parents[1])
    exe = CAMEL
    bad = cells = 0
    with tempfile.TemporaryDirectory() as t:
        work = Path(args.keep or t)
        work.mkdir(parents=True, exist_ok=True)
        programs = [(f"p{k}", *make_program(rng, k)) for k in range(args.programs)]
        programs += [(f"a{k}", *make_arms_program(random.Random(f"{args.seed} arms {k}"), k)) for k in range(args.programs)]
        for k, source, expected in programs:
            (work / f"{k}.cml").write_text(source, encoding="utf-8")
            for passes in PIPELINES:
                cells += 1
                proc = subprocess.run([exe, f"{k}.cml", *passes], cwd=work, env={**os.environ, "CAMEL_HOME": home, ENV_NAME: str(ENV_VALUE)},
                                      capture_output=True, text=True, timeout=120)
                got = [line for line in proc.stdout.splitlines() if line.strip()]
                if proc.returncode != 0 or got != expected:
                    bad += 1
                    diff = [f"{g!r} (expected {e!r})" for g, e in zip(got, expected) if g != e][:3]
                    print(f"program {k} [{' '.join(passes) or 'default'}]: exit {proc.returncode}; "
                          f"{len(got)} of {len(expected)} lines; first differences: {diff}; stderr: {proc.stderr[-200:]!r}")
                elif "Warn" in proc.stderr and args.warnings:
                    print(f"program {k} [{' '.join(passes) or 'default'}]: {proc.stderr[:300]!r}")
    print(f"var pipelines: seed {args.seed}, {cells} program-pipeline cells, {bad} wrong")
    print("PASS" if bad == 0 and cells > 0 else "FAIL")
    return 0 if bad == 0 and cells > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
