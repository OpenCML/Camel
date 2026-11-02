"""The smallest attention block of C3, in numpy, with the gradient worked out by hand.

The block (identical to `block_forward.cml`, `block_library_grad.cml` and `block_engine_grad.cml`):

    x    = [[1.0, 0.5], [0.5, 1.0]]
    wq   = [[1.0], [0.5]]      wk = [[0.5], [1.0]]      wv = [[1.0], [-1.0]]
    q, k, v = x wq, x wk, x wv
    s    = q k^T / sqrt(2)
    s'   = s + mask              mask = 0 on and below the diagonal, -1000 above it
    a    = softmax(s', rows)     row 0 has one live key, row 1 has two
    out  = sum(a v)

`out` is one number and there are six parameters (two entries of each of the three 2x1 matrices),
so the comparison is six numbers against six.  This file answers them two ways:

  * **analytic**: the derivative of `out` with respect to each parameter, written out below from
    the chain rule.  This is the reading compare.py puts beside Camel's;
  * **finite difference**: a central difference of the same forward pass, which is an independent
    check on the analytic derivation (it would catch a wrong sign or a dropped term in it).

torch is used instead when it can be imported, because a reference both parties agree on is better
than one this example wrote; a machine without torch still gets a reading.

    python showcase/c3-char-transformer/baseline/block.py
    python showcase/c3-char-transformer/baseline/block.py --h 1e-6 --json block.json
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

SQRT2 = np.sqrt(2.0)

X = np.array([[1.0, 0.5], [0.5, 1.0]], dtype=np.float64)
WQ = np.array([[1.0], [0.5]], dtype=np.float64)
WK = np.array([[0.5], [1.0]], dtype=np.float64)
WV = np.array([[1.0], [-1.0]], dtype=np.float64)
MASK = np.array([[0.0, -1000.0], [0.0, 0.0]], dtype=np.float64)

# The order the six parameters are reported in, everywhere in this example.
NAMES = ["wq.0", "wq.1", "wk.0", "wk.1", "wv.0", "wv.1"]


def softmax_rows(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=1, keepdims=True)


def forward(weights):
    """The block, with `weights` the six numbers in `NAMES` order."""
    wq = np.array([[weights[0]], [weights[1]]], dtype=np.float64)
    wk = np.array([[weights[2]], [weights[3]]], dtype=np.float64)
    wv = np.array([[weights[4]], [weights[5]]], dtype=np.float64)
    q, k, v = X @ wq, X @ wk, X @ wv
    scores = (q @ k.T) / SQRT2
    a = softmax_rows(scores + MASK)
    o = a @ v
    return float(o.sum()), dict(scores=scores, a=a, v=v, q=q, k=k)


def analytic(weights):
    """d out / d weights, by the chain rule, in the same order as `NAMES`."""
    value, c = forward(weights)
    q, k, v, a, scores = c["q"], c["k"], c["v"], c["a"], c["scores"]
    # out = sum(a v):  do/do = 1 elementwise
    do = np.ones_like(v)                              # [2, 1]
    # o = a v  =>  da = do v^T,  dv = a^T do
    da = do @ v.T                                     # [2, 2]
    dv = a.T @ do                                     # [2, 1]
    # a = softmax(s') row by row  =>  ds' = a * (da - sum(a * da, axis=1, keepdims))
    ds = a * (da - (a * da).sum(axis=1, keepdims=True))
    # s = q k^T / sqrt(2)  =>  dq = ds k / sqrt(2),  dk = ds^T q / sqrt(2)
    dq = (ds @ k) / SQRT2
    dk = (ds.T @ q) / SQRT2
    # q = x wq  =>  dwq = x^T dq
    dwq = X.T @ dq
    dwk = X.T @ dk
    dwv = X.T @ dv
    return value, [float(dwq[0, 0]), float(dwq[1, 0]), float(dwk[0, 0]), float(dwk[1, 0]),
                   float(dwv[0, 0]), float(dwv[1, 0])]


def finite_difference(weights, h):
    gradient = []
    for index in range(len(weights)):
        up = list(weights)
        down = list(weights)
        up[index] += h
        down[index] -= h
        gradient.append((forward(up)[0] - forward(down)[0]) / (2.0 * h))
    return gradient


def torch_reading(weights):
    """The same block in torch, differentiated by autograd, when torch is importable."""
    try:
        import torch
    except Exception:
        return None
    x = torch.tensor(X, dtype=torch.float64)
    wq = torch.tensor([[weights[0]], [weights[1]]], dtype=torch.float64, requires_grad=True)
    wk = torch.tensor([[weights[2]], [weights[3]]], dtype=torch.float64, requires_grad=True)
    wv = torch.tensor([[weights[4]], [weights[5]]], dtype=torch.float64, requires_grad=True)
    scores = (x @ wq) @ (x @ wk).T / torch.sqrt(torch.tensor(2.0, dtype=torch.float64))
    attention = torch.softmax(scores + torch.tensor(MASK, dtype=torch.float64), dim=1)
    out = (attention @ (x @ wv)).sum()
    out.backward()
    return float(out), [float(wq.grad[0, 0]), float(wq.grad[1, 0]), float(wk.grad[0, 0]),
                        float(wk.grad[1, 0]), float(wv.grad[0, 0]), float(wv.grad[1, 0])]


def relative(a, b):
    denominator = max(abs(a), abs(b), 1e-12)
    return abs(a - b) / denominator


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--h", type=float, default=1e-6, help="the finite difference's step")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    weights = [float(WQ[0, 0]), float(WQ[1, 0]), float(WK[0, 0]), float(WK[1, 0]),
               float(WV[0, 0]), float(WV[1, 0])]
    value, gradient = analytic(weights)
    difference = finite_difference(weights, args.h)
    print(f"block.numpy forward={value:.12f}")
    for name, number in zip(NAMES, gradient):
        print(f"block.numpy analytic.{name}={number:.12f}")
    for name, number in zip(NAMES, difference):
        print(f"block.numpy difference.{name}={number:.12f}")
    worst = max(relative(a, b) for a, b in zip(gradient, difference))
    print(f"block.numpy check worst={worst:.3e} step={args.h:g}")

    reference = "hand-derived analytic Jacobian"
    torch_reading_ = torch_reading(weights)
    if torch_reading_ is not None:
        torch_value, torch_gradient = torch_reading_
        torch_worst = max(relative(a, b) for a, b in zip(gradient, torch_gradient))
        print(f"block.numpy torch_forward={torch_value:.12f}")
        for name, number in zip(NAMES, torch_gradient):
            print(f"block.numpy torch.{name}={number:.12f}")
        print(f"block.numpy torch_check worst={torch_worst:.3e}")
        reference = "hand-derived analytic Jacobian, cross-checked against torch.autograd"
    print(f"block.numpy method=analytic reference={reference}")

    if args.json:
        Path(args.json).write_text(json.dumps({
            "forward": value,
            "names": NAMES,
            "analytic": gradient,
            "finite_difference": difference,
            "worst_analytic_vs_difference": worst,
            "reference": reference,
        }, indent=2), encoding="utf-8")
        print(f"block.numpy json={args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())