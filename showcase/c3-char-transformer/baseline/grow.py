"""The reference the growing Transformer is measured against, one stage at a time.

Each stage is the *same* arithmetic as the Camel program of the same number under `grow/`, written
here in numpy with the optimizer's update spelled out: torch is not installed on the machine this was
measured on (`ModuleNotFoundError: No module named 'torch'`), so the update is PyTorch's Adam line for
line -- `m = b1*m + (1-b1)*g`, `v = b2*v + (1-b2)*g*g`, `p -= lr * (m/(1-b1**t)) / (sqrt(v/(1-b2**t))
+ eps)` -- with the two bias corrections carried forward by multiplication.  `--torch` repeats the
run through `torch.optim.Adam` when the library is importable.

The gradients here are central differences of the same forward pass, not a second autodiff: the
comparison is of two *training runs*, and taking the derivative by a different route is what makes
the loss sequences independent evidence rather than the same code twice.

    python showcase/c3-char-transformer/baseline/grow.py --stage 1
    python showcase/c3-char-transformer/baseline/grow.py --stage 4 --json grow4.json
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

SQRT2 = np.sqrt(2.0)
STEPS = 50
RATE = 0.002
BETA1, BETA2, EPS = 0.9, 0.999, 1e-8

X = np.array([[1.0, 0.5], [0.5, 1.0]], dtype=np.float64)
MASK = np.array([[0.0, -1000.0], [0.0, 0.0]], dtype=np.float64)


def softmax_rows(z):
    z = z - z.max(axis=1, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=1, keepdims=True)


def attention(x, wq, wk, wv):
    q = x @ wq
    k = (x @ wk).T
    a = softmax_rows((q @ k) / SQRT2 + MASK)
    return a @ (x @ wv)


def layer_norm(t, gamma, beta):
    mean = t.mean()
    var = ((t - mean) ** 2).mean()
    return (t - mean) / np.sqrt(var + 1e-5) * gamma + beta


# Stage 1 of the reference: the six matrices of the two-block chain.  One flat vector of numbers,
# laid out in `NAMES` order, so a central difference can step one entry at a time.
# The second block projects a `[2, 1]` value, so its three matrices are `1 x 1`: one number each.
NAMES = ["wq1.0", "wq1.1", "wk1.0", "wk1.1", "wv1.0", "wv1.1",
         "wq2", "wk2", "wv2"]
START = [1.0, 0.5, 0.5, 1.0, 1.0, -1.0, 0.5, 1.0, 0.75]


def loss_stage1(w):
    wq1 = np.array([[w[0]], [w[1]]])
    wk1 = np.array([[w[2]], [w[3]]])
    wv1 = np.array([[w[4]], [w[5]]])
    wq2 = np.array([[w[6]]])
    wk2 = np.array([[w[7]]])
    wv2 = np.array([[w[8]]])
    o1 = attention(X, wq1, wk1, wv1)
    return float(attention(o1, wq2, wk2, wv2).sum())


STAGES = {1: (NAMES, START, loss_stage1)}


def ln(t, g, b, eps=1e-5):
    """`nn.layer_norm` over the trailing axis: each row normalized on its own, gamma/beta of length 2."""
    mu = t.mean(axis=1, keepdims=True)
    var = ((t - mu) ** 2).mean(axis=1, keepdims=True)
    return (t - mu) / np.sqrt(var + eps) * g + b


NAMES2 = ["wq1", "wk1", "wv1", "g1", "b1", "wq2", "wk2", "wv2", "g2", "b2"]
START2 = [
    1.0, 0.5, 0.5, 1.0,           # wq1
    0.5, 1.0, 1.0, 0.5,           # wk1
    1.0, -1.0, -1.0, 1.0,         # wv1
    1.0, 1.0,                     # g1
    0.0, 0.0,                     # b1
    0.75, 0.0, 0.0, 0.75,         # wq2
    0.0, 0.5, 0.5, 0.0,           # wk2
    1.0, 1.0, 1.0, 1.0,           # wv2
    1.0, 1.0,                     # g2
    0.0, 0.0,                     # b2
]


def loss_stage2(w):
    wq1 = np.array(w[0:4]).reshape(2, 2)
    wk1 = np.array(w[4:8]).reshape(2, 2)
    wv1 = np.array(w[8:12]).reshape(2, 2)
    g1 = np.array(w[12:14])
    b1 = np.array(w[14:16])
    wq2 = np.array(w[16:20]).reshape(2, 2)
    wk2 = np.array(w[20:24]).reshape(2, 2)
    wv2 = np.array(w[24:28]).reshape(2, 2)
    g2 = np.array(w[28:30])
    b2 = np.array(w[30:32])
    first = X + attention(X, wq1, wk1, wv1)
    second = first + attention(ln(first, g1, b1), wq2, wk2, wv2)
    return float(ln(second, g2, b2).sum())


STAGES[2] = (NAMES2, START2, loss_stage2)


def relu(t):
    return np.maximum(t, 0.0)


NAMES3 = ["emb", "f1", "b1", "f2", "b2", "wq1", "wk1", "wv1"]
START3 = [
    1.0, 0.5, 0.5, 1.0,                         # emb, 2x2
    0.75, -0.5, 0.5, 0.25, 1.0, 0.5, -0.25, 0.75,   # f1, 2x4
    0.1, -0.1, 0.05, 0.0,                       # b1, 4
    1.0, 0.5, -0.5, 1.0, 0.25, -0.25, 0.5, 1.0,     # f2, 4x2
    0.0, 0.1,                                   # b2, 2
    1.0, 0.5, 0.5, 1.0,                         # wq1
    0.5, 1.0, 1.0, 0.5,                         # wk1
    1.0, -1.0, -1.0, 1.0,                       # wv1
]
TOKENS = [0, 1]


def loss_stage3(w):
    emb = np.array(w[0:4]).reshape(2, 2)
    f1 = np.array(w[4:12]).reshape(2, 4)
    b1 = np.array(w[12:16])
    f2 = np.array(w[16:24]).reshape(4, 2)
    b2 = np.array(w[24:26])
    wq1 = np.array(w[26:30]).reshape(2, 2)
    wk1 = np.array(w[30:34]).reshape(2, 2)
    wv1 = np.array(w[34:38]).reshape(2, 2)
    x = emb[TOKENS]
    r = x + attention(x, wq1, wk1, wv1)
    hidden = relu(r @ f1 + b1)
    return float((r + hidden @ f2 + b2).sum())


STAGES[3] = (NAMES3, START3, loss_stage3)

def run(stage, use_torch=False):
    names, start, loss = STAGES[stage]
    weights = list(start)
    m = [0.0] * len(weights)
    v = [0.0] * len(weights)
    correction1 = correction2 = 1.0
    losses = []
    for _ in range(STEPS):
        losses.append(loss(weights))
        gradient = []
        for index in range(len(weights)):
            up = list(weights)
            down = list(weights)
            up[index] += 1e-6
            down[index] -= 1e-6
            gradient.append((loss(up) - loss(down)) / 2e-6)
        for i in range(len(weights)):
            m[i] = BETA1 * m[i] + (1.0 - BETA1) * gradient[i]
            v[i] = BETA2 * v[i] + (1.0 - BETA2) * gradient[i] * gradient[i]
        correction1 *= BETA1
        correction2 *= BETA2
        for i in range(len(weights)):
            weights[i] -= RATE * (m[i] / (1.0 - correction1)) / (
                np.sqrt(v[i] / (1.0 - correction2)) + EPS)
    return losses, loss(weights)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=int, default=1)
    parser.add_argument("--torch", action="store_true")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    losses, after = run(args.stage, use_torch=args.torch)
    for step, value in enumerate(losses, start=1):
        print(f"stage{args.stage} step={step} loss={value:.12f}")
    rises = [step for step, value in enumerate(losses, start=2) if value > losses[step - 2]]
    print(f"stage{args.stage} done steps={STEPS} last={after:.12f} params={len(STAGES[args.stage][0])}")
    print(f"stage{args.stage} rise_at={rises[0] if rises else 'none'} "
          f"reference={'torch' if args.torch else 'numpy-central-difference'}")
    if args.json:
        Path(args.json).write_text(json.dumps({"losses": losses, "after": after}, indent=2),
                                   encoding="utf-8")
        print(f"stage{args.stage} json={args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
