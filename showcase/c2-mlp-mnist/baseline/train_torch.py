#!/usr/bin/env python3
"""C2 baseline: the MLP of showcase/c2-mlp-mnist/train.cml in PyTorch.

The counterpart a practitioner would write: `nn.Linear` layers, `F.cross_entropy`,
`torch.optim.Adam` and a `state_dict` checkpoint.  It reads the same data and the same
starting weights as the Camel program, so the two runs differ only in the code.

The `# == name ==` lines delimit the sections compare.py counts, so the two implementations
can be lined up side by side in RESULTS.md.

Run:
  python train_torch.py --data artifacts/small --steps 200 --batch 50 --lr 0.001 \
      --log-every 20 --threads 1
"""

from __future__ import annotations

# == imports ==
import argparse
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from torch import nn


# == model ==
class MLP(nn.Module):
    """784 -> hidden -> 10, ReLU in between.  The same shapes as the Camel `Mlp` struct."""

    def __init__(self, hidden: int):
        super().__init__()
        self.fc1 = nn.Linear(784, hidden)
        self.fc2 = nn.Linear(hidden, 10)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.fc2(F.relu(self.fc1(x)))


# == loss ==
def loss(logits: torch.Tensor, labels: torch.Tensor) -> torch.Tensor:
    return F.cross_entropy(logits, labels)


# == data ==
def load_model(data: Path) -> MLP:
    """The starting weights come from the data directory: both implementations load them."""
    init = data / "init"
    w1 = torch.from_numpy(np.load(init / "w1.npy"))
    model = MLP(hidden=w1.shape[1])
    with torch.no_grad():
        model.fc1.weight.copy_(w1.T)  # nn.Linear stores [out, in]
        model.fc1.bias.copy_(torch.from_numpy(np.load(init / "b1.npy")))
        model.fc2.weight.copy_(torch.from_numpy(np.load(init / "w2.npy")).T)
        model.fc2.bias.copy_(torch.from_numpy(np.load(init / "b2.npy")))
    return model


# == evaluate ==
def evaluate(model: MLP, x: torch.Tensor, labels: torch.Tensor) -> tuple[float, float]:
    with torch.no_grad():
        logits = model(x)
        value = loss(logits, labels)
        accuracy = (logits.argmax(dim=1) == labels).float().mean()
    return value.item(), accuracy.item()


# == entry ==
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", required=True, help="directory make_data.py wrote")
    parser.add_argument("--steps", type=int, required=True)
    parser.add_argument("--batch", type=int, required=True)
    parser.add_argument("--lr", type=float, required=True)
    parser.add_argument("--log-every", type=int, default=20)
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args()

    torch.set_num_threads(args.threads)

    # == data ==
    data = Path(args.data)
    train_x = torch.from_numpy(np.load(data / "train_images.npy"))
    train_labels = torch.from_numpy(np.load(data / "train_labels.npy"))
    val_x = torch.from_numpy(np.load(data / "val_images.npy"))
    val_labels = torch.from_numpy(np.load(data / "val_labels.npy"))
    # train_order.npy holds one epoch permutation after another, flat.  Both implementations
    # walk this order, so a step sees the same rows on both sides.
    train_order = np.load(data / "train_order.npy")
    rows = len(train_x)
    dataset_id = (data / "dataset.id").read_text(encoding="utf-8").strip()

    model = load_model(data)
    parameters = sum(p.numel() for p in model.parameters())
    batches = rows // args.batch
    before = evaluate(model, val_x, val_labels)
    print(
        f"c2 config steps={args.steps} batch={args.batch} lr={args.lr} threads={args.threads} "
        f"params={parameters} batches={batches}"
    )
    print(f"c2 init val_loss={before[0]:.9f} val_accuracy={before[1]:.9f}")

    # == train ==
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    started = time.perf_counter()
    for step in range(args.steps):
        offset = (step // batches) * rows + (step % batches) * args.batch
        chosen = torch.from_numpy(train_order[offset : offset + args.batch])
        x = train_x[chosen]
        y = train_labels[chosen]
        value = loss(model(x), y)
        optimizer.zero_grad()
        value.backward()
        optimizer.step()
        if step % args.log_every == 0:
            print(f"c2 step={step} loss={value.item():.9f}")
    elapsed = time.perf_counter() - started

    # == evaluate ==
    after = evaluate(model, val_x, val_labels)
    print(
        f"c2 trained steps={args.steps} seconds={elapsed:.6f} "
        f"val_loss={after[0]:.9f} val_accuracy={after[1]:.9f}"
    )

    # == checkpoint ==
    checkpoint = data / "checkpoint-torch.pt"
    torch.save({"model": model.state_dict(), "dataset_id": dataset_id}, checkpoint)
    saved = torch.load(checkpoint, weights_only=True)
    assert saved["dataset_id"] == dataset_id
    model.load_state_dict(saved["model"])
    restored = evaluate(model, val_x, val_labels)
    drift = sum(
        (model.state_dict()[name] - saved["model"][name]).abs().sum().item()
        for name in saved["model"]
    )
    print(
        f"c2 checkpoint val_loss={restored[0]:.9f} val_accuracy={restored[1]:.9f} "
        f"l1_drift={drift:.9f}"
    )
    print("c2 leaves " + ", ".join(saved["model"].keys()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())