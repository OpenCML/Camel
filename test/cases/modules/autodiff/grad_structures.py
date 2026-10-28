"""Gradient check over generated programs: Camel's reverse mode against central finite differences.

Programs are generated from a fixed seed as a chain of STAGES applied to a value that depends on
the differentiated parameter. The stages are the structures a derivative has to be routed through:
a called function, a let-bound lambda with captures, a nested function with a capture, a branch
whose arms are calls (returning one value or a tuple), and plain operators in between — in random
order, so that the result of one structure feeds the next (a call feeding a closure, a call
feeding a branch arm, ...). Each program is written twice from the same description: as Camel
source and as a NumPy float64 function. The gradient Camel computes (float32) must agree with
central finite differences of the NumPy function.

Why this exists: two silently wrong gradients (a closure call and a tuple-valued branch whose
argument was the result of another call) passed every hand-written test, because those tests
exercised each structure alone.

Usage: python grad_structures.py [--seed N] [--programs N]   (needs numpy)
"""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
TOL = 2e-2  # relative to the largest element of the reference gradient (float32 against FD)


def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-z))


UNARY = [
    ("tanh({v})", "np.tanh({v})"),
    ("sigmoid({v})", "sigmoid({v})"),
    ("({v} * 0.7 + 0.2)", "({v} * 0.7 + 0.2)"),
    ("tanh({v} * 1.3)", "np.tanh({v} * 1.3)"),
    ("({v} * {v})", "({v} * {v})"),
]


class Gen:
    """One loss function `loss_k(b: f32[..]): float`, as Camel text and as Python text."""

    def __init__(self, rng: random.Random, index: int):
        self.rng, self.index = rng, index
        self.helpers_c: list[str] = []
        self.helpers_p: list[str] = []
        self.body_c: list[str] = []
        self.body_p: list[str] = []
        self.n = 0

    def name(self, stem: str) -> str:
        self.n += 1
        return f"{stem}{self.index}_{self.n}"

    def unary(self, v: str) -> tuple[str, str]:
        c, p = self.rng.choice(UNARY)
        return c.format(v=v), p.format(v=v)

    def let(self, c: str, p: str) -> str:
        v = self.name("v")
        self.body_c.append(f"    let {v} = {c}")
        self.body_p.append(f"    {v} = {p}")
        return v

    def stage_plain(self, v: str) -> str:
        return self.let(*self.unary(v))

    def stage_call(self, v: str) -> str:
        f = self.name("helper")
        c1, p1 = self.unary("a")
        c2, p2 = self.unary("t")
        self.helpers_c.append(f"func {f}(a: f32[..]): f32[..] {{\n    let t = {c1}\n    return {c2}\n}}\n")
        self.helpers_p.append(f"def {f}(a):\n    t = {p1}\n    return {p2}\n")
        return self.let(f"{f}({v})", f"{f}({v})")

    def stage_lambda(self, v: str) -> str:
        s = self.let(f"mean<0, true>({v}) * 1.3", f"{v}.mean(0, keepdims=True) * 1.3")
        g = self.name("g")
        c1, p1 = self.unary("p")
        self.body_c.append(f"    let {g} = (p: f32[..]): f32[..] => {{\n        let q = {c1}\n        tanh(q * {s}) + {s}\n    }}")
        self.body_p.append(f"    def {g}(p):\n        q = {p1}\n        return np.tanh(q * {s}) + {s}")
        return self.let(f"{g}({v})", f"{g}({v})")

    def stage_nested(self, v: str) -> str:
        s = self.let(f"float(sum({v}))", f"{v}.sum()")
        g = self.name("inner_fn")
        c1, p1 = self.unary("p")
        self.body_c.append(f"    func {g}(p: f32[..]): f32[..] {{\n        return {c1} * f32({s})\n    }}")
        self.body_p.append(f"    def {g}(p):\n        return {p1} * {s}")
        return self.let(f"{g}({v})", f"{g}({v})")

    def stage_branch(self, v: str) -> str:
        fa, fb = self.name("arm"), self.name("arm")
        c1, p1 = self.unary("y")
        c2, p2 = self.unary("y")
        if self.rng.random() < 0.5:  # arms return one value
            self.helpers_c.append(f"func {fa}(y: f32[..]): f32[..] {{\n    return {c1}\n}}\n")
            self.helpers_c.append(f"func {fb}(y: f32[..]): f32[..] {{\n    return {c2} * 0.5\n}}\n")
            self.helpers_p.append(f"def {fa}(y):\n    return {p1}\n")
            self.helpers_p.append(f"def {fb}(y):\n    return {p2} * 0.5\n")
            return self.let(f"if float(sum({v})) > 0.1 then {fa}({v}) else {fb}({v})", f"{fa}({v}) if {v}.sum() > 0.1 else {fb}({v})")
        # arms return a tuple whose second component also depends on the argument
        self.helpers_c.append(
            f"func {fa}(y: f32[..]): (f32[..], f32[..]) {{\n    let t = {c1}\n    return (t, tanh(mean<0, true>(t)))\n}}\n")
        self.helpers_c.append(
            f"func {fb}(y: f32[..]): (f32[..], f32[..]) {{\n    let t = {c2}\n    return (t * 0.5, mean<0, true>(y))\n}}\n")
        self.helpers_p.append(f"def {fa}(y):\n    t = {p1}\n    return (t, np.tanh(t.mean(0, keepdims=True)))\n")
        self.helpers_p.append(f"def {fb}(y):\n    t = {p2}\n    return (t * 0.5, y.mean(0, keepdims=True))\n")
        a, b = self.name("pa"), self.name("pb")
        self.body_c.append(f"    let ({a}, {b}) = if float(sum({v})) > 0.1 then {fa}({v}) else {fb}({v})")
        self.body_p.append(f"    {a}, {b} = {fa}({v}) if {v}.sum() > 0.1 else {fb}({v})")
        return self.let(f"{a} + {b}", f"{a} + {b}")

    def build(self) -> tuple[str, str, list[str]]:
        stages = [self.stage_plain, self.stage_call, self.stage_lambda, self.stage_nested, self.stage_branch]
        v = self.let("tanh(b * 0.9) + 0.3", "np.tanh(b * 0.9) + 0.3")
        kinds = []
        for _ in range(self.rng.randint(3, 5)):
            stage = self.rng.choice(stages)
            kinds.append(stage.__name__[6:])
            v = stage(v)
        name = f"loss{self.index}"
        camel = "".join(self.helpers_c) + f"with <b: f32[..]>\nfunc {name}(): float {{\n" + "\n".join(self.body_c) + f"\n    return float(sum({v}))\n}}\n"
        python = "".join(self.helpers_p) + f"def {name}(b):\n" + "\n".join(self.body_p) + f"\n    return float({v}.sum())\n"
        return camel, python, kinds


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=20261007)
    ap.add_argument("--programs", type=int, default=6)
    ap.add_argument("--losses", type=int, default=8)
    ap.add_argument("--keep")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    x = np.array([0.3, -0.2, 0.7])
    worst, bad, total = 0.0, 0, 0
    with tempfile.TemporaryDirectory() as t:
        work = Path(args.keep or t)
        work.mkdir(parents=True, exist_ok=True)
        for k in range(args.programs):
            camel, python, kinds = [], [], []
            for j in range(args.losses):
                c, p, kd = Gen(rng, j).build()
                camel.append(c)
                python.append(p)
                kinds.append(kd)
            source = (
                "import { mean, reshape, save_npy, sigmoid, sum, tanh } from tensor\n"
                "import { grad } from autodiff\n\n" + "\n".join(camel) + "\nfunc main(): int sync {\n"
                "    let xs: f32[] = [0.3, -0.2, 0.7]\n" + "    let x = reshape<[3]>(xs)\n"
                + "".join(f"    save_npy(grad(loss{j})<x>(), 'g{k}_{j}.npy')\n" for j in range(args.losses))
                + "    return 0\n}\n")
            (work / f"p{k}.cml").write_text(source, encoding="utf-8")
            (work / f"p{k}.py").write_text("\n".join(python), encoding="utf-8")
            for passes in (["std::macro"], []):
                env = {**os.environ}
                env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
                proc = subprocess.run([str(exe), f"p{k}.cml", *passes], cwd=work, env=env, capture_output=True, text=True)
                label = " ".join(passes) or "default"
                if proc.returncode != 0:
                    print(f"program {k} [{label}]: camel failed: {(proc.stdout + proc.stderr)[-400:]}")
                    bad += 1
                    continue
                scope = {"np": np, "sigmoid": sigmoid}
                exec("\n".join(python), scope)
                for j in range(args.losses):
                    f = scope[f"loss{j}"]
                    fd = np.array([(f(x + e) - f(x - e)) / 2e-6 for e in np.eye(3) * 1e-6])
                    got = np.load(work / f"g{k}_{j}.npy").astype(np.float64).reshape(-1)
                    err = float(np.abs(got - fd).max() / max(np.abs(fd).max(), 1e-4))
                    total += 1
                    worst = max(worst, err)
                    if not err < TOL:
                        bad += 1
                        print(f"program {k} loss {j} [{label}] stages {kinds[j]}: camel {got} fd {fd} rel err {err:.2e}")
    print(f"grad structures: seed {args.seed}, {total} gradients checked, {bad} wrong, worst rel err {worst:.2e}")
    print("PASS" if bad == 0 and total > 0 else "FAIL")
    return 0 if bad == 0 and total > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
