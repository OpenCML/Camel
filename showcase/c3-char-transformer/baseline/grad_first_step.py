"""The gradient of stage 2's model at its initial point, one tensor per line, in the same format
`grow/grad_first_step.cml` prints it -- so the two can be compared element by element.

    python showcase/c3-char-transformer/baseline/grad_first_step.py
    python showcase/c3-char-transformer/baseline/grad_first_step.py --engine engine.txt

`--engine` reads the Camel case's stdout and reports, next to the reference, how many tensors and
elements were compared, the largest absolute and relative difference, which tensors are identically
zero, and whether any shape disagrees.  A complex step (`loss(x + ih).imag / h`) is the reference
here for the reason stage 2 taught: this model's first loss is zero for every parameter but the last
beta, so a central difference's own error is amplified by Adam's division by sqrt(v) + 1e-8 into
something that looks like an engine defect.  There is nothing to cancel in a complex step.
"""
import argparse
import ast
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import grow  # noqa: E402  (the model and its start are stage 2's, in one place)

SHAPES2 = {
    "wq1": (2, 2), "wk1": (2, 2), "wv1": (2, 2), "g1": (2,), "b1": (2,),
    "wq2": (2, 2), "wk2": (2, 2), "wv2": (2, 2), "g2": (2,), "b2": (2,),
}
ORDER2 = ["wq1", "wk1", "wv1", "g1", "b1", "wq2", "wk2", "wv2", "g2", "b2"]

SHAPES4 = {
    "emb": (2, 2), "g1a": (2,), "b1a": (2,), "wq1": (2, 2), "wk1": (2, 2), "wv1": (2, 2),
    "g1b": (2,), "b1b": (2,), "f1_1": (2, 4), "fb1_1": (4,), "f2_1": (4, 2), "fb2_1": (2,),
    "g2a": (2,), "b2a": (2,), "wq2": (2, 2), "wk2": (2, 2), "wv2": (2, 2), "g2b": (2,), "b2b": (2,),
    "f1_2": (2, 4), "fb1_2": (4,), "f2_2": (4, 2), "fb2_2": (2,), "gout": (2,), "bout": (2,),
}
ORDER4 = ["emb", "g1a", "b1a", "wq1", "wk1", "wv1", "g1b", "b1b", "f1_1", "fb1_1", "f2_1",
          "fb2_1", "g2a", "b2a", "wq2", "wk2", "wv2", "g2b", "b2b", "f1_2", "fb1_2", "f2_2",
          "fb2_2", "gout", "bout"]

LAYOUT = {
    2: (SHAPES2, ORDER2, lambda: grow.START2, grow.loss_stage2),
    4: (SHAPES4, ORDER4, lambda: grow.START4, grow.loss_stage4),
}


def offsets_of(shapes, order):
    offsets, at = {}, 0
    for name in order:
        size = int(np.prod(shapes[name]))
        offsets[name] = (at, at + size)
        at += size
    return offsets


def gradient(stage):
    """Every element of the model's gradient at its start, by a complex step."""
    _, _, start_of, loss = LAYOUT[stage]
    weights = list(start_of())
    h = 1e-30
    out = []
    for i in range(len(weights)):
        probe = list(weights)
        probe[i] = probe[i] + 1j * h
        out.append(float(loss(probe).imag / h))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=int, default=2, choices=(2, 4))
    parser.add_argument("--engine", default=None,
                        help="a file holding the matching grad_first_step case's stdout")
    args = parser.parse_args()

    shapes, order, start_of, loss = LAYOUT[args.stage]
    offsets = offsets_of(shapes, order)

    def nested(flat, name):
        a = np.array(flat[offsets[name][0]:offsets[name][1]], dtype=np.float64)
        return a.reshape(shapes[name]).tolist()

    flat = gradient(args.stage)
    print(f"grad loss={loss(list(start_of())).real:.12f}")
    for name in order:
        print(f"grad {name} {nested(flat, name)!r}")

    if not args.engine:
        return 0

    text = Path(args.engine).read_text(encoding="utf-8")
    seen = {}
    for line in text.splitlines():
        m = re.match(r"^grad (\w+) (\[.*\])$", line.strip())
        if m:
            seen[m.group(1)] = ast.literal_eval(m.group(2))
    missing = [n for n in order if n not in seen]
    shape_bad = [n for n in order if n in seen and np.shape(seen[n]) != shapes[n]]
    worst_abs = worst_rel = 0.0
    worst_abs_at = worst_rel_at = ""
    zeros = []
    compared = 0
    for name in order:
        if name not in seen:
            continue
        mine = np.array(flat[offsets[name][0]:offsets[name][1]]).reshape(shapes[name])
        theirs = np.array(seen[name], dtype=np.float64)
        if mine.shape != theirs.shape:
            continue
        compared += mine.size
        if np.all(mine == 0.0):
            zeros.append(name)
        d = np.abs(mine - theirs)
        idx = np.unravel_index(int(np.argmax(d)), d.shape)
        if d[idx] > worst_abs:
            worst_abs, worst_abs_at = float(d[idx]), f"{name}[{','.join(map(str, idx))}]"
        with np.errstate(divide="ignore", invalid="ignore"):
            rel = d / np.maximum(np.abs(mine), 1e-300)
        rel = np.where(np.abs(mine) == 0.0, 0.0, rel)
        ridx = np.unravel_index(int(np.argmax(rel)), rel.shape)
        if rel[ridx] > worst_rel:
            worst_rel, worst_rel_at = float(rel[ridx]), f"{name}[{','.join(map(str, ridx))}]"
    print(f"compare tensors={len(order) - len(missing)} elements={compared} "
          f"max_abs={worst_abs:.3e} at {worst_abs_at} max_rel={worst_rel:.3e} at {worst_rel_at}")
    print(f"compare missing={missing or 'none'} shape_mismatch={shape_bad or 'none'} "
          f"identically_zero={zeros or 'none'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())