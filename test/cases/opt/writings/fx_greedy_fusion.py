#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The **control** for the ECOOP measurement (a): the same greedy fusion on torch.fx.

Plan 丙's acceptance item (a) says the transform written in Camel must fuse two writings of
`x*2 + y*3 + x*y` to one graph, and that the same transform written against torch.fx must fuse them
to two. The second half is what makes the first a statement about **this** language: in torch.fx a
node's place in `Graph.nodes` is the order the Python was executed in, i.e. the writing, so a
transform that pairs "adjacent" multiplications by walking `Graph.nodes` pairs different
multiplications in the two writings. Nothing in torch.fx is wrong; it has no canonical order and does
not claim one.

This script is that second half, and it is why the plan entry is not here: **it needs a Python with
`torch`, and the build host has none** (2026-11-18: the host's `~/camel-eval/venv-build` and
`venv-bench` carry cmake/ninja/conan and onnx/onnxruntime, no torch; and the host cannot reach PyPI
from the campus network). A plan entry that skipped for want of an interpreter would put a skip in
the gate where nothing is measured, so this file is run on the ECOOP side:

    python -m pip install torch
    python test/cases/opt/writings/fx_greedy_fusion.py

and its source is quoted in `internal/reports/2026-11-18-eco-measurements.cn.md`.

The transform is the same one, step for step, as `greedy_fuse.cml.in`:

  * take the multiplications among the graph's nodes **in the graph's own order**;
  * pair them two at a time, the first with the second, the third with the fourth;
  * fuse each pair into one node -- the second is absorbed into the first (`replace_all_uses_with`
    then `erase_node`, the counterpart of the graph module's `replace`).

Readings, printed one per line so that "different" is a reading and not an absence:

    torch: 2.x.y
    writing base: nodes=[...] fused=[...]
    writing swapped: nodes=[...] fused=[...]
    verdict: the two writings fuse to the same graph = false
    fx_greedy_fusion: the two writings fuse differently, which is the control = PASS

Exit status: 0 the control holds (the two writings fuse differently), 1 it does not, 2 no torch.
"""

import operator
import sys

PROGRAM = "x * 2 + y * 3 + x * y"


def base(x, y):
    a = x * 2
    b = y * 3
    c = x * y
    return a + b + c


def swapped(x, y):
    b = y * 3
    a = x * 2
    c = x * y
    return a + b + c


def renamed(left, right):
    first = left * 2
    second = right * 3
    third = left * right
    return first + second + third


WRITINGS = (("base", base), ("swapped", swapped), ("renamed", renamed))


def is_mul(node):
    return node.op == "call_function" and getattr(node.target, "__name__", "") == "mul"


def fuse(gm):
    """The greedy fusion of plan item (a), on one traced graph: pair adjacent multiplications."""
    nodes = list(gm.graph.nodes)
    muls = [node for node in nodes if is_mul(node)]
    fused = 0
    for i in range(0, len(muls) - 1, 2):
        first, second = muls[i], muls[i + 1]
        second.replace_all_uses_with(first)
        gm.graph.erase_node(second)
        fused += 1
    gm.graph.lint()
    gm.recompile()
    return fused


def describe(gm):
    """The graph by what it computes, ids erased: each node as `op(#input, ...)` in written order."""
    index = {}
    words = []
    for position, node in enumerate(gm.graph.nodes):
        index[node] = position
        arguments = []
        for argument in node.args:
            if hasattr(argument, "op"):
                arguments.append(f"#{index[argument]}")
            elif isinstance(argument, (int, float, bool, str)) or argument is None:
                arguments.append(repr(argument))
            else:
                arguments.append(type(argument).__name__)
        words.append(f"{getattr(node.target, '__name__', node.op)}({', '.join(arguments)})")
    return words


def main():
    try:
        import torch
        from torch.fx import symbolic_trace
    except ImportError as error:
        print(f"fx_greedy_fusion: no torch on this interpreter ({error})")
        print("fx_greedy_fusion: this control runs on the ECOOP side; see the docstring")
        return 2

    print(f"torch: {torch.__version__}")
    print(f"program: {PROGRAM}")
    fused = {}
    for name, function in WRITINGS:
        gm = symbolic_trace(function)
        before = describe(gm)
        pairs = fuse(gm)
        after = describe(gm)
        fused[name] = after
        print(f"writing {name}: fused_pairs={pairs}")
        print(f"  nodes= {before}")
        print(f"  fused= {after}")

    same = fused["base"] == fused["swapped"]
    print(f"verdict: the two writings fuse to the same graph = {'true' if same else 'false'}")
    print(f"verdict: the renamed writing fuses to the same graph as base = "
          f"{'true' if fused['renamed'] == fused['base'] else 'false'}")

    # What makes this a control and not a repeat: the swapped pair must part here. The renamed pair
    # may or may not -- a rename moves nothing in `Graph.nodes`, so on torch.fx too it is the same
    # graph, and that is the second half of the criterion: only the order of the statements is
    # visible, never the names.
    if same:
        print("fx_greedy_fusion: the two writings fused to one graph, so this is no control: FAIL")
        return 1
    print("fx_greedy_fusion: the two writings fuse differently, which is the control = PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())