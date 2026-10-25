"""Reference gradients for grad_function_values.cml and grad_function_values_effects.cml.

Each loss of the Camel cases is written again as a NumPy float64 function, independently of the
compiler, and differentiated by central finite differences. Run it to print the references;
with --check FILE it reads Camel's printed gradients (one `tensor([...])` line per loss, in
order) and compares them (float32 against float64 finite differences); with --check-camel CAMEL
it runs grad_function_values.cml beside this file with that compiler and compares what it prints,
so the gradients the plan pins are checked against an implementation that shares no code with it.

Usage: python grad_function_values_reference.py [--check FILE | --check-camel CAMEL]   (needs numpy)
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
X = np.array([0.5, 0.25, -0.35], dtype=np.float64)
TOL = 2e-3  # relative to the largest element of the reference gradient


def make(s):
    return lambda t: np.tanh(t * s)


def make_affine(q):
    return lambda t: t * q + q


def apply(f, p):
    return f(p) + f(p * 0.5)


def apply_outer(f, p):
    return apply(f, p * p) * 0.5


def square(t):
    return t * t


def twice(f):
    return lambda t: f(f(t))


def loss1(p):
    s = np.sum(p)
    g = lambda t: np.tanh(t * s)
    h = lambda t: g(t) * 2.0 + g(t * 0.5)
    return np.sum(h(p))


def loss2(p):
    s = np.sum(p * p)
    g = lambda t: np.tanh(t * s)
    h = lambda t: g(t) * s
    k = lambda t: h(t + 1.0) + t
    return np.sum(k(p))


def loss3(p):
    g = make(np.sum(p))
    return np.sum(g(p) * g(p * 0.5))


def loss4(p):
    g = make_affine(np.tanh(p) * 2.0)
    return np.sum(g(p * p))


def loss5(p):
    s = np.sum(p)
    return np.sum(apply(lambda t: np.tanh(t * s), p))


def loss6(p):
    return np.sum(apply(square, p))


def loss7(p):
    s = np.sum(p)
    return np.sum(apply_outer(lambda t: np.tanh(t * s), p))


def loss8(p):
    return np.sum(apply(make(np.sum(p)), p))


def loss9(p):
    s = np.sum(p)
    g = lambda t: np.tanh(t * s)
    return np.sum(twice(g)(p))


def loss10(p):
    s = np.sum(p)
    mk = lambda a: (lambda t: np.tanh(t * a) * s)
    return np.sum(mk(s * 0.5)(p))


# grad_function_values_effects.cml (the prints do not change the values)
def effects1(p):
    s = np.sum(p)
    return np.sum(apply(lambda t: np.tanh(t * s), p))


def effects2(p):
    g = make(np.sum(p))
    return np.sum(g(p) * g(p * 0.5))


def effects3(p):
    s = np.sum(p)
    g = lambda t: np.tanh(t * s)
    h = lambda t: g(t) * 2.0 + g(t * 0.5)
    return np.sum(h(p))


# grad_function_values_arms.cml
def arms1(p):
    s = np.sum(p)
    g = lambda t: np.tanh(t * s)
    return np.sum(g(p)) if s > 0.0 else np.sum(g(p * 2.0)) * 0.5


def arms2(p):
    s = np.sum(p)
    g = lambda t: np.tanh(t * s)
    return np.sum(g(p)) if s < 0.0 else np.sum(g(p * 2.0)) * 0.5


SUITES = {
    "values": [loss1, loss2, loss3, loss4, loss5, loss6, loss7, loss8, loss9, loss10],
    "effects": [effects1, effects2, effects3],
    "arms": [arms1, arms2],
}


def fd(f, x, h=1e-6):
    g = np.zeros_like(x)
    for i in range(x.size):
        e = np.zeros_like(x)
        e[i] = h
        g[i] = (f(x + e) - f(x - e)) / (2 * h)
    return g


def compare(rows: list[str], losses: list, refs: list) -> int:
    if len(rows) != len(refs):
        print(f"expected {len(refs)} gradients, found {len(rows)}")
        return 1
    bad = 0
    for f, ref, row in zip(losses, refs, rows):
        got = np.array([float(v) for v in row.split(",")])
        err = np.max(np.abs(got - ref)) / max(np.max(np.abs(ref)), 1e-12)
        ok = err <= TOL
        bad += 0 if ok else 1
        print(f"{f.__name__}: {'ok' if ok else 'MISMATCH'} rel_err={err:.2e} camel={got} fd={ref}")
    print(f"{len(refs)} gradients checked, {bad} wrong")
    return 1 if bad else 0


def main() -> int:
    suite = "values"
    args = sys.argv[1:]
    if args and args[0] in SUITES:
        suite = args.pop(0)
    losses = SUITES[suite]
    refs = [fd(f, X) for f in losses]
    if len(args) == 2 and args[0] == "--check":
        text = open(args[1], encoding="utf-8").read()
        return compare(re.findall(r"tensor\(\[([^\]]*)\]", text), losses, refs)
    if len(args) == 2 and args[0] == "--check-camel":
        env = {**os.environ}
        env.setdefault("CAMEL_HOME", str(HERE.parents[3] / "out" / "latest"))
        proc = subprocess.run(
            [args[1], str(HERE / "grad_function_values.cml")], cwd=HERE, env=env, capture_output=True, text=True
        )
        if proc.returncode != 0:
            print(f"camel failed: {(proc.stdout + proc.stderr)[-400:]}")
            return 1
        return compare(re.findall(r"tensor\(\[([^\]]*)\]", proc.stdout), losses, refs)
    for f, ref in zip(losses, refs):
        print(f.__name__, np.array2string(ref, precision=6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
