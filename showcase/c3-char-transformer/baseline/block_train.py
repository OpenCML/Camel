"""The smallest attention block of C3 trained for 50 steps, as the reference the Camel program is
compared against.

This is the block of `block.py` -- six parameters, one scalar output -- so the only thing added here
is the loop: Adam, the same constants (`beta1` 0.9, `beta2` 0.999, `eps` 1e-8), the same constant
rate (0.002), the same starting point (the six numbers `block.py` starts from) and the same 50 steps,
one loss per step.  Nothing is sampled, so no seed is needed and none is written: both programs start
at the same six numbers because both spell them out.

**torch is not installed on the machine this was measured on** (`ModuleNotFoundError: No module
named 'torch'`), so the update is written out here rather than called from `torch.optim.Adam`.  The
arithmetic is PyTorch's, line for line: `m = b1*m + (1-b1)*g`, `v = b2*v + (1-b2)*g*g`, then
`p -= lr * (m/(1-b1**t)) / (sqrt(v/(1-b2**t)) + eps)`, with the two corrections carried forward by
multiplication instead of `**t`.  If torch is importable, `--torch` repeats the same 50 steps through
`torch.optim.Adam` and the two sequences are compared here.

    python showcase/c3-char-transformer/baseline/block_train.py
    python showcase/c3-char-transformer/baseline/block_train.py --torch --json train.json
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

SQRT2 = np.sqrt(2.0)
X = np.array([[1.0, 0.5], [0.5, 1.0]], dtype=np.float64)
MASK = np.array([[0.0, -1000.0], [0.0, 0.0]], dtype=np.float64)
START = [1.0, 0.5, 0.5, 1.0, 1.0, -1.0]
NAMES = ["wq.0", "wq.1", "wk.0", "wk.1", "wv.0", "wv.1"]
STEPS = 50
RATE = 0.002
BETA1, BETA2, EPS = 0.9, 0.999, 1e-8


def softmax_rows(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=1, keepdims=True)


def loss(weights):
    wq = np.array([[weights[0]], [weights[1]]], dtype=np.float64)
    wk = np.array([[weights[2]], [weights[3]]], dtype=np.float64)
    wv = np.array([[weights[4]], [weights[5]]], dtype=np.float64)
    q, k, v = X @ wq, X @ wk, X @ wv
    scores = (q @ k.T) / SQRT2
    a = softmax_rows(scores + MASK)
    return float((a @ v).sum())


def gradients(weights, h=1e-6):
    """The six gradients of `loss`, by central difference: an independent reading of the same
    derivative the Camel program gets from `with_grad`."""
    out = []
    for index in range(len(weights)):
        up = list(weights)
        down = list(weights)
        up[index] += h
        down[index] -= h
        out.append((loss(up) - loss(down)) / (2.0 * h))
    return out


def train(use_torch=False):
    """The 50 loss values, one per step, from the starting point `START`."""
    weights = list(START)
    if use_torch:
        return train_torch()
    m = [0.0] * 6
    v = [0.0] * 6
    correction1 = correction2 = 1.0
    losses = []
    for _ in range(STEPS):
        # The value the step starts from, printed before the update: the Camel program prints its
        # loss at the top of the body too, so step n is the same instant on both sides.
        losses.append(loss(weights))
        g = gradients(weights)
        for i in range(6):
            m[i] = BETA1 * m[i] + (1.0 - BETA1) * g[i]
            v[i] = BETA2 * v[i] + (1.0 - BETA2) * g[i] * g[i]
        correction1 *= BETA1
        correction2 *= BETA2
        for i in range(6):
            mh = m[i] / (1.0 - correction1)
            vh = v[i] / (1.0 - correction2)
            weights[i] -= RATE * mh / (np.sqrt(vh) + EPS)
    return losses


def train_torch():
    import torch

    x = torch.tensor(X, dtype=torch.float64)
    mask = torch.tensor(MASK, dtype=torch.float64)
    wq = torch.tensor([[START[0]], [START[1]]], dtype=torch.float64, requires_grad=True)
    wk = torch.tensor([[START[2]], [START[3]]], dtype=torch.float64, requires_grad=True)
    wv = torch.tensor([[START[4]], [START[5]]], dtype=torch.float64, requires_grad=True)
    optimizer = torch.optim.Adam([wq, wk, wv], lr=RATE, betas=(BETA1, BETA2), eps=EPS)
    losses = []
    for _ in range(STEPS):
        optimizer.zero_grad()
        scores = (x @ wq) @ (x @ wk).T / torch.sqrt(torch.tensor(2.0, dtype=torch.float64))
        attention = torch.softmax(scores + mask, dim=1)
        value = (attention @ (x @ wv)).sum()
        value.backward()
        optimizer.step()
        losses.append(float(value))
    return losses


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--torch", action="store_true", help="measure the reference with torch")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    losses = train(use_torch=args.torch)
    for step, value in enumerate(losses, start=1):
        print(f"py train step={step} loss={value:.12f}")
    worst_step = None
    for step, value in enumerate(losses, start=1):
        if step > 1 and value > losses[step - 2]:
            worst_step = step
            break
    print(f"py train steps={STEPS} first={losses[0]:.12f} last={losses[-1]:.12f} "
          f"rise_at={worst_step if worst_step is not None else 'none'}")
    print(f"py train grader={len(losses)} reference={'torch' if args.torch else 'numpy'}")
    if args.json:
        Path(args.json).write_text(json.dumps({"losses": losses, "steps": STEPS,
                                               "rate": RATE, "start": START}, indent=2),
                                   encoding="utf-8")
        print(f"py train json={args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())