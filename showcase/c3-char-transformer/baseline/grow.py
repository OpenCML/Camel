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
    # `z.real` for the shift so a complex step (--exact) can run through this unchanged: the shift
    # is a constant of the row, and a comparison on a complex number is not defined.
    z = z - z.real.max(axis=1, keepdims=True)
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
    return ln(second, g2, b2).sum()


STAGES[2] = (NAMES2, START2, loss_stage2)

# Stage 4: the whole model the author asked for -- embedding, two post-layer-norm blocks with a
# residual each, a final layer_norm.  Same numbers as `grow/stage4_full_model.cml`, laid out in the
# order of its `Model`, so one flat vector steps one entry at a time.
NAMES4 = [
    "emb", "g1a", "b1a", "wq1", "wk1", "wv1", "g1b", "b1b", "f1_1", "fb1_1", "f2_1", "fb2_1",
    "g2a", "b2a", "wq2", "wk2", "wv2", "g2b", "b2b", "f1_2", "fb1_2", "f2_2", "fb2_2",
    "gout", "bout",
]
START4 = [
    1.0, 0.5, 0.5, 1.0,                                       # emb, 2x2
    1.0, 1.0, 0.0, 0.0,                                       # g1a, b1a
    1.0, 0.5, 0.5, 1.0,                                       # wq1
    0.5, 1.0, 1.0, 0.5,                                       # wk1
    1.0, -1.0, -1.0, 1.0,                                     # wv1
    1.0, 1.0, 0.0, 0.0,                                       # g1b, b1b
    0.75, -0.5, 0.5, 0.25, 1.0, 0.5, -0.25, 0.75,             # f1_1, 2x4
    0.1, -0.1, 0.05, 0.0,                                     # fb1_1, 4
    1.0, 0.5, -0.5, 1.0, 0.25, -0.25, 0.5, 1.0,               # f2_1, 4x2
    0.0, 0.1,                                                 # fb2_1, 2
    1.0, 1.0, 0.0, 0.0,                                       # g2a, b2a
    0.75, 0.0, 0.0, 0.75,                                     # wq2
    0.0, 0.5, 0.5, 0.0,                                       # wk2
    1.0, 1.0, 1.0, 1.0,                                       # wv2
    1.0, 1.0, 0.0, 0.0,                                       # g2b, b2b
    0.5, 0.25, 0.75, -0.5, 0.25, 0.75, 0.5, 0.5,              # f1_2, 2x4
    0.0, 0.05, -0.05, 0.1,                                    # fb1_2, 4
    0.5, 1.0, 1.0, 0.25, -0.25, 0.5, 0.75, -0.5,              # f2_2, 4x2
    0.1, 0.0,                                                 # fb2_2, 2
    1.0, 1.0, 0.0, 0.0,                                       # gout, bout
]


def forward4(w):
    """The stage-4 model's output rows for TOKENS, before the final normalisation."""
    emb = np.array(w[0:4]).reshape(2, 2)
    g1a, b1a = np.array(w[4:6]), np.array(w[6:8])
    wq1 = np.array(w[8:12]).reshape(2, 2)
    wk1 = np.array(w[12:16]).reshape(2, 2)
    wv1 = np.array(w[16:20]).reshape(2, 2)
    g1b, b1b = np.array(w[20:22]), np.array(w[22:24])
    f1_1 = np.array(w[24:32]).reshape(2, 4)
    fb1_1 = np.array(w[32:36])
    f2_1 = np.array(w[36:44]).reshape(4, 2)
    fb2_1 = np.array(w[44:46])
    g2a, b2a = np.array(w[46:48]), np.array(w[48:50])
    wq2 = np.array(w[50:54]).reshape(2, 2)
    wk2 = np.array(w[54:58]).reshape(2, 2)
    wv2 = np.array(w[58:62]).reshape(2, 2)
    g2b, b2b = np.array(w[62:64]), np.array(w[64:66])
    f1_2 = np.array(w[66:74]).reshape(2, 4)
    fb1_2 = np.array(w[74:78])
    f2_2 = np.array(w[78:86]).reshape(4, 2)
    fb2_2 = np.array(w[86:88])
    gout, bout = np.array(w[88:90]), np.array(w[90:92])
    x = emb[TOKENS]
    attended1 = x + attention(ln(x, g1a, b1a), wq1, wk1, wv1)
    hidden1 = relu(ln(attended1, g1b, b1b) @ f1_1 + fb1_1)
    first = attended1 + hidden1 @ f2_1 + fb2_1
    attended2 = first + attention(ln(first, g2a, b2a), wq2, wk2, wv2)
    hidden2 = relu(ln(attended2, g2b, b2b) @ f1_2 + fb1_2)
    return attended2 + hidden2 @ f2_2 + fb2_2


def loss_stage4(w):
    gout, bout = np.array(w[88:90]), np.array(w[90:92])
    return ln(forward4(w), gout, bout).sum()


STAGES[4] = (NAMES4, START4, loss_stage4)


def relu(t):
    # Not `np.maximum(t, 0.0)`, so that a complex step keeps the derivative of the branch it is in.
    return np.where(t.real > 0.0, t, 0.0)


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

def gradient_of(loss, weights, exact):
    """One gradient vector: a complex step when `exact`, a central difference otherwise.

    A complex step is not an approximation here in the way a difference is: `loss(x + ih)` has the
    derivative in its imaginary part to machine precision, with no subtractive cancellation, so
    `--exact` is the same trajectory the engine computes rather than one that shares the engine's
    answer only to as many digits as the step and the curvature allow.  That distinction is what
    stage 2 needed: its first step's loss is zero for every parameter but the last beta, so Adam's
    squared average is zero there and the division by `sqrt(v) + 1e-8` magnifies whatever a
    difference left behind -- 8.0e-11 at step 2 grew to 3.0e-6 by step 50.
    """
    H = 1e-30
    out = []
    for index in range(len(weights)):
        if exact:
            up = list(weights)
            up[index] = up[index] + 1j * H
            out.append(loss(up).imag / H)
            continue
        up = list(weights)
        down = list(weights)
        up[index] += 1e-6
        down[index] -= 1e-6
        out.append((loss(up) - loss(down)) / 2e-6)
    return out


def run(stage, use_torch=False, exact=False, steps=None, return_weights=False):
    names, start, loss = STAGES[stage]
    weights = list(start)
    m = [0.0] * len(weights)
    v = [0.0] * len(weights)
    correction1 = correction2 = 1.0
    losses = []
    for _ in range(STEPS if steps is None else steps):
        losses.append(loss(weights))
        gradient = gradient_of(loss, weights, exact)
        for i in range(len(weights)):
            m[i] = BETA1 * m[i] + (1.0 - BETA1) * gradient[i]
            v[i] = BETA2 * v[i] + (1.0 - BETA2) * gradient[i] * gradient[i]
        correction1 *= BETA1
        correction2 *= BETA2
        for i in range(len(weights)):
            weights[i] -= RATE * (m[i] / (1.0 - correction1)) / (
                np.sqrt(v[i] / (1.0 - correction2)) + EPS)
    if return_weights:
        return losses, loss(weights), weights
    return losses, loss(weights)


HEAD_START = [[0.5, -0.5], [-0.5, 0.5]]
LABEL = np.array([[0.0, 1.0], [1.0, 0.0]])


def forward5(w):
    """Stage 5's logits: stage 4's output rows through the head."""
    head = np.array(w[92:96]).reshape(2, 2)
    return forward4(w) @ head


def loss_stage5(w):
    """Cross entropy against LABEL -- each row's target class, in log space."""
    z = forward5(w)
    denominator = np.log(np.exp(z) @ np.ones((2, 2)))
    return (denominator * LABEL).sum() - (z * LABEL).sum()


NAMES5 = NAMES4 + ["head"]
START5 = START4 + [0.5, -0.5, -0.5, 0.5]
STAGES[5] = (NAMES5, START5, loss_stage5)

def forward6(w):
    """Stage 6's logits: stage 4's rows plus a learned position embedding, through the head."""
    position = np.array(w[96:100]).reshape(2, 2)
    head = np.array(w[92:96]).reshape(2, 2)
    return (forward4(w) + position) @ head


def loss_stage6(w):
    """The same cross entropy as stage 5, on rows that can now tell the two positions apart."""
    z = forward6(w)
    denominator = np.log(np.exp(z) @ np.ones((2, 2)))
    return (denominator * LABEL).sum() - (z * LABEL).sum()


NAMES6 = NAMES5 + ["pos"]
START6 = START5 + [0.0, 0.0, 0.0, 0.0]
STAGES[6] = (NAMES6, START6, loss_stage6)

def forward7(w):
    """Stage 7: one head of self attention over the two positions, then the head."""
    head = np.array(w[92:96]).reshape(2, 2)
    position = np.array(w[96:100]).reshape(2, 2)
    wq = np.array(w[100:104]).reshape(2, 2)
    wk = np.array(w[104:108]).reshape(2, 2)
    wv = np.array(w[108:112]).reshape(2, 2)
    x = forward4(w) + position
    q = x @ wq
    k = (x @ wk).T
    a = softmax_rows(q @ k * 0.70710678118654752)
    return a @ (x @ wv) @ head


def loss_stage7(w):
    """The same cross entropy as stage 5 and 6, on logits that attention has mixed."""
    z = forward7(w)
    denominator = np.log(np.exp(z) @ np.ones((2, 2)))
    return (denominator * LABEL).sum() - (z * LABEL).sum()


NAMES7 = NAMES6 + ["wq", "wk", "wv"]
START7 = START6 + [0.5, 0.0, 0.0, 0.5, 0.5, 0.0, 0.0, 0.5, 0.5, 0.0, 0.0, 0.5]
STAGES[7] = (NAMES7, START7, loss_stage7)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=int, default=1)
    parser.add_argument("--torch", action="store_true")
    parser.add_argument("--exact", action="store_true",
                        help="complex-step gradients (machine precision) instead of central differences")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    losses, after = run(args.stage, use_torch=args.torch, exact=args.exact)
    for step, value in enumerate(losses, start=1):
        print(f"stage{args.stage} step={step} loss={value:.12f}")
    rises = [step for step, value in enumerate(losses, start=2) if value > losses[step - 2]]
    print(f"stage{args.stage} done steps={STEPS} last={after:.12f} params={len(STAGES[args.stage][0])}")
    print(f"stage{args.stage} rise_at={rises[0] if rises else 'none'} "
          f"reference={'torch' if args.torch else ('numpy-complex-step' if args.exact else 'numpy-central-difference')}")
    if args.json:
        Path(args.json).write_text(
            json.dumps({"losses": [float(x) for x in losses], "after": float(after)}, indent=2),
            encoding="utf-8")
        print(f"stage{args.stage} json={args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
