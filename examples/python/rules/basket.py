#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""A Python project whose pricing rules are a Camel module.

The Python side keeps everything that is Python's job -- the batches, the report, NumPy -- and the
rules live in `rules.cml`: one function per tier, checked when the module loads rather than when a
branch happens to run, reachable from Python as ordinary functions, and with one array entry point
that a NumPy array reaches without being copied.

    pip install camel-lang
    python examples/python/rules/basket.py

The first command is what a project does once; the second is what it runs.

The example also prices the same baskets three ways -- Camel, plain Python, and NumPy -- because
the honest answer to "which one" depends on the rules, and the numbers are what tells you. See
README.md, which is about that choice as much as about the binding.
"""

import sys
import time
from pathlib import Path

import numpy as np

import camel

HERE = Path(__file__).resolve().parent

# One call, once, at import time: the module is loaded, and every `export func` in it is checked
# before a single basket is priced.
rules = camel.load(str(HERE / "rules.cml"))

BASKETS = 1_000_000


# The same rules written in Python, to show that the two agree -- and to show what the Python side
# would have to carry if the rules did not live in the module.
def discount_in_python(total):
    if total >= 2000.0:
        return 0.20
    if total >= 1000.0:
        return 0.15
    if total >= 500.0:
        return 0.10
    if total >= 100.0:
        return 0.05
    return 0.0


def shipping_in_python(total):
    return 0.0 if total >= 300.0 else 9.90


def net_total_in_python(total):
    return total * (1.0 - discount_in_python(total)) + shipping_in_python(total)


def net_total_vectorized(totals):
    """The same rules with NumPy: fast, and the shape a rule set stops fitting into."""
    rate = np.select(
        [totals >= 2000.0, totals >= 1000.0, totals >= 500.0, totals >= 100.0],
        [0.20, 0.15, 0.10, 0.05],
        0.0,
    )
    shipping = np.where(totals >= 300.0, 0.0, 9.90)
    return totals * (1.0 - rate) + shipping


def main():
    print("rules module:", rules.name, "from", rules.path)

    print()
    print("  basket    discount   shipping   net total   net (Python)")
    for total in (80.0, 120.0, 480.0, 620.0, 1200.0, 2500.0):
        net = rules.net_total(total)
        print(
            f"  {total:>8.2f}  {rules.discount(total):>8.2%}  {rules.shipping(total):>8.2f}"
            f"  {net:>10.2f}   {net_total_in_python(total):>12.2f}"
        )

    print()
    rng = np.random.default_rng(7)
    baskets = (rng.uniform(0.0, 3000.0, BASKETS) * 100.0).astype(np.float32)
    print(f"{BASKETS:,} baskets, one NumPy array of f32 ({baskets.nbytes / 2**20:.0f} MiB)")

    # The array is handed to the module as a DLPack capsule, so nothing is copied on the way in;
    # what costs time below is the rules running over every basket, not the crossing.
    started = time.perf_counter()
    net = rules.net_totals(baskets)
    camel_seconds = time.perf_counter() - started

    started = time.perf_counter()
    by_python = [net_total_in_python(float(total)) for total in baskets]
    python_seconds = time.perf_counter() - started

    started = time.perf_counter()
    by_numpy = net_total_vectorized(baskets)
    numpy_seconds = time.perf_counter() - started

    print(f"  Camel:        {camel_seconds * 1e3:8.1f} ms, rules as functions in rules.cml")
    print(f"  plain Python: {python_seconds * 1e3:8.1f} ms, the same rules written again here")
    print(f"  NumPy:        {numpy_seconds * 1e3:8.1f} ms, the same rules as array expressions")

    # The answer is the answer the Python rules give, element for element.
    expected = np.array(by_python[:200_000], dtype=np.float32)
    worst = float(np.max(np.abs(expected - net[:200_000])))
    numpy_worst = float(np.max(np.abs(np.asarray(by_numpy[:200_000], dtype=np.float32) - net[:200_000])))
    print(
        f"  agreement:    Camel against Python {worst:.2e}, against NumPy {numpy_worst:.2e} "
        f"(largest net total here is {float(np.max(expected)):.0f})"
    )
    if not np.allclose(expected, net[:200_000], rtol=1e-5, atol=1e-3):
        print("Camel and the Python rules do not agree", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())