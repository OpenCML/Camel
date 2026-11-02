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

SHAPES = {
    "wq1": (2, 2), "wk1": (2, 2), "wv1": (2, 2), "g1": (2,), "b1": (2,),
    "wq2": (2, 2), "wk2": (2, 2), "wv2": (2, 2), "g2": (2,), "b2": (2,),
}
ORDER = ["wq1", "wk1", "wv1", "g1", "b1", "wq2", "wk2", "wv2", "g2", "b2"]
OFFSETS = {}
_at = 0
for _name in ORDER:
    _size = int(np.prod(SHAPES[_name]))
    OFFSETS[_name] = (_at, _at + _size)
    _at += _size


def gradient():
    """Every element of stage 2's gradient at `grow.START2`, by a complex step."""
    weights = list(grow.START2)
    h = 1e-30
    out = []
    for i in range(len(weights)):
        probe = list(weights)
        probe[i] = probe[i] + 1j * h
        out.append(float(grow.loss_stage2(probe).imag / h))
    return out


def nested(flat, name):
    a = np.array(flat[OFFSETS[name][0]:OFFSETS[name][1]], dtype=np.float64).reshape(SHAPES[name])
    return a.tolist()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", default=None,
                        help="a file holding grow/grad_first_step.cml's stdout, to compare")
    args = parser.parse_args()

    flat = gradient()
    print(f"grad loss={grow.loss_stage2(list(grow.START2)).real:.12f}")
    for name in ORDER:
        print(f"grad {name} {nested(flat, name)!r}")

    if not args.engine:
        return 0

    text = Path(args.engine).read_text(encoding="utf-8")
    seen = {}
    for line in text.splitlines():
        m = re.match(r"^grad (\w+) (\[.*\])$", line.strip())
        if m:
            seen[m.group(1)] = ast.literal_eval(m.group(2))
    missing = [n for n in ORDER if n not in seen]
    shape_bad = [n for n in ORDER
                 if n in seen and np.shape(seen[n]) != SHAPES[n]]
    worst_abs = worst_rel = 0.0
    worst_abs_at = worst_rel_at = ""
    zeros = []
    compared = 0
    for name in ORDER:
        if name not in seen:
            continue
        mine = np.array(flat[OFFSETS[name][0]:OFFSETS[name][1]]).reshape(SHAPES[name])
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
    print(f"compare tensors={len(ORDER) - len(missing)} elements={compared} "
          f"max_abs={worst_abs:.3e} at {worst_abs_at} max_rel={worst_rel:.3e} at {worst_rel_at}")
    print(f"compare missing={missing or 'none'} shape_mismatch={shape_bad or 'none'} "
          f"identically_zero={zeros or 'none'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())