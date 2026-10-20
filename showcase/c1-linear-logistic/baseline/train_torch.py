# C1 -- the PyTorch counterpart of train.cml.
#
# Same model, same loss, same SGD, same minibatches and the same starting point (zeros).  The
# only difference is the implementation: `nn.Linear` and `F.mse_loss` /
# `F.binary_cross_entropy_with_logits` instead of a struct of tensors and the hand-written
# formulas.

import argparse
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


# == model ==
class Model(nn.Module):
    def __init__(self, w: np.ndarray, b: np.ndarray):
        super().__init__()
        self.linear = nn.Linear(2, 1, bias=True)
        with torch.no_grad():
            self.linear.weight.copy_(torch.from_numpy(w.T.copy()))
            self.linear.bias.copy_(torch.from_numpy(b.copy()))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.linear(x)


# == loss ==
def loss(z: torch.Tensor, y: torch.Tensor, kind: int) -> torch.Tensor:
    if kind == 0:
        return F.mse_loss(z, y)
    return F.binary_cross_entropy_with_logits(z, y)


# == data ==
def load(data: Path):
    init = data / "init"
    w = np.load(init / "w.npy")
    b = np.load(init / "b.npy")
    return (
        torch.from_numpy(np.load(data / "train_x.npy")),
        torch.from_numpy(np.load(data / "train_y.npy")),
        torch.from_numpy(np.load(data / "val_x.npy")),
        torch.from_numpy(np.load(data / "val_y.npy")),
        w, b,
    )


# == evaluate ==
def evaluate(model: Model, x: torch.Tensor, y: torch.Tensor, kind: int):
    with torch.no_grad():
        z = model(x)
        if kind == 0:
            errors = (z - y).abs()
            return F.mse_loss(z, y).item(), errors.mean().item()
        correct = ((z > 0.0) == (y > 0.5)).float()
        return F.binary_cross_entropy_with_logits(z, y).item(), correct.mean().item()


# == entry ==
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--model", choices=["linear", "logistic"], required=True)
    parser.add_argument("--steps", type=int, default=50)
    parser.add_argument("--batch", type=int, default=32)
    parser.add_argument("--lr", type=float, default=0.1)
    parser.add_argument("--log_every", type=int, default=10)
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args()

    torch.set_num_threads(args.threads)
    kind = 0 if args.model == "linear" else 1
    data = Path(args.data)
    train_x, train_y, val_x, val_y, w, b = load(data)
    model = Model(w, b)
    optimizer = torch.optim.SGD(model.parameters(), lr=args.lr)

    # == train ==
    params = sum(p.numel() for p in model.parameters())
    batches = train_x.shape[0] // args.batch
    before = evaluate(model, val_x, val_y, kind)
    print(f"c1 config model={args.model} steps={args.steps} batch={args.batch} "
          f"lr={args.lr} threads={args.threads} params={params} batches={batches}", flush=True)
    print(f"c1 init loss={before[0]:.9f} metric={before[1]:.9f}", flush=True)

    started = time.perf_counter()
    for step in range(args.steps):
        start = (step % batches) * args.batch
        x = train_x[start:start + args.batch]
        y = train_y[start:start + args.batch]
        optimizer.zero_grad(set_to_none=True)
        value = loss(model(x), y, kind)
        if step % args.log_every == 0:
            print(f"c1 step={step} loss={value.item():.9f}", flush=True)
        value.backward()
        optimizer.step()
    elapsed = time.perf_counter() - started

    # == evaluate ==
    after = evaluate(model, val_x, val_y, kind)
    print(f"c1 trained steps={args.steps} seconds={elapsed:.6f} "
          f"loss={after[0]:.9f} metric={after[1]:.9f}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())