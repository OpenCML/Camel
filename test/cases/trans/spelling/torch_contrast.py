#!/usr/bin/env python3
"""The contrast the spelling lane's acceptance asks for: torch.fx on the same two writings.

`test/cases/trans/spelling/canonical_pairing.py` shows that a transform written in Camel, pairing
the multiplications of `nodes` in the canonical order, gives one answer for two writings of one
program. This script runs the same measurement on a graph library whose default order is the order
the program was written in: `torch.fx.symbolic_trace` of the two writings, then the same greedy
pairing over the traced graph's node order.

The program is `x * 2 + y * 3 + x * y` with a second `x * 2` (so that there is an equal-product
pair to find), written twice: in the first writing the two `x * 2` are neighbours, in the second a
`y * 3` stands between them and every local is renamed. Nothing about the computation differs.

A greedy pairing that reads the traced order fuses one pair in the first writing and none in the
second. The Camel acceptance fuses the same program in both writings and the two results compare
equal byte for byte.

This is not a plan entry, for the reason `test/cases/opt/writings/fx_greedy_fusion.py` gives: it
needs a Python with `torch`, and a plan entry that skipped for want of an interpreter would put
a skip in the gate where there is nothing to measure. The host `zjwei` does have it -- the probe
on 2026-11-18 answered `torch 2.14.1+cu130` with the interpreter a plan entry would use -- so the
readings below were taken there, and the file is quoted in
`docs/handover/reports/2026-11-18-commit-checks-and-spelling-spelling.cn.md`.

Run: python test/cases/trans/spelling/torch_contrast.py
Exit 0 when torch is present (the answer is the reading), 77 when it is not.
"""

import sys

# The same program written twice. A: the two `x * 2` are written one after the other. B: the `y * 3`
# is written between them and every local is renamed.
WRITING_A = """\
def writing(x, y):
    a = x * 2
    d = x * 2
    b = y * 3
    c = x * y
    return a + b + c + d
"""

WRITING_B = """\
def writing(p, q):
    t = p * 2
    u = q * 3
    v = p * 2
    w = p * q
    return v + u + w + t
"""


def product_of(node, positions):
    """What a `mul` node multiplies, with a placeholder named by its position.

    `x * 2` in the first writing and `p * 2` in the second are the same product: the parameter is
    the first one in both.
    """
    parts = []
    for argument in node.args:
        if hasattr(argument, "op"):  # a node: a placeholder, or another product
            parts.append(("node", positions.get(argument.name, argument.name)))
        else:
            parts.append(("const", argument))
    return tuple(sorted(parts, key=str))


def greedy_fuse(graph):
    """Pair adjacent equal products in the order the graph lists them, one pair at a time."""
    positions = {}
    for node in graph.graph.nodes:
        if node.op == "placeholder":
            positions[node.name] = len(positions)
    operations = [node for node in graph.graph.nodes if node.op == "call_function"]
    order = [(node, node.target.__name__) for node in operations]
    fused = []
    index = 0
    while index + 1 < len(order):
        (left, left_kind), (right, right_kind) = order[index], order[index + 1]
        if (
            left_kind == "mul"
            and right_kind == "mul"
            and product_of(left, positions) == product_of(right, positions)
        ):
            fused.append((left.name, right.name))
            index += 2
            continue
        index += 1
    return order, fused


def main():
    try:
        import torch
        import torch.fx as fx
    except Exception as error:  # noqa: BLE001 - the point is to report why it cannot run
        print(f"no torch on this machine: {error}")
        return 77

    print(f"torch {torch.__version__}")
    answers = []
    for label, source in (("A", WRITING_A), ("B", WRITING_B)):
        namespace = {}
        exec(source, namespace)  # noqa: S102 - the two writings are the input of the measurement
        traced = fx.symbolic_trace(namespace["writing"])
        order, fused = greedy_fuse(traced)
        print(f"--- writing {label}: {len(order)} operation(s) in the order it was written ---")
        print("  " + ", ".join(kind for _, kind in order))
        print(f"  the greedy pairing fuses {len(fused)} pair(s)" + (f": {fused}" if fused else ""))
        answers.append((traced.code, order, fused))

    same_text = answers[0][0] == answers[1][0]
    print(f"the two writings trace to one graph, text and all: {same_text}")
    print(
        f"the two writings give the greedy pairing one answer: {answers[0][2] == answers[1][2]}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())