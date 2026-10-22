# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E9: pretrain the MLP backbone on upright MNIST (torch, Adam, 3 epochs), then
save the backbone weights plus zero-initialized LoRA adapters (rank 8, same
deterministic A init as make_init.py's rng(2001)) to artifacts/lora/pretrained/.

Also reports the "before adaptation" numbers: test accuracy on upright MNIST
and on the user's rotated eval set (make_mnist_user.py must run first).
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
RANK = 8
LAYERS = (("fc1", 784, 256), ("fc2", 256, 128), ("fc3", 128, 10))


def main() -> int:
    import torch
    from torch import nn

    data = np.load(BENCH_ROOT.parent / ".cache" / "mnist" / "mnist.npz")
    x_train = torch.from_numpy((data["x_train"].astype(np.float32) / 255.0).reshape(-1, 784))
    y_train = torch.from_numpy(data["y_train"].astype(np.int64))
    x_test = torch.from_numpy((data["x_test"].astype(np.float32) / 255.0).reshape(-1, 784))
    y_test = torch.from_numpy(data["y_test"].astype(np.int64))

    torch.manual_seed(4001)
    net = nn.Sequential(
        nn.Linear(784, 256), nn.ReLU(), nn.Linear(256, 128), nn.ReLU(), nn.Linear(128, 10)
    )
    opt = torch.optim.Adam(net.parameters(), lr=1e-3)
    lossf = nn.CrossEntropyLoss()
    steps = 0
    for epoch in range(3):
        perm = torch.randperm(x_train.shape[0])
        for i in range(0, x_train.shape[0], 256):
            idx = perm[i : i + 256]
            opt.zero_grad()
            loss = lossf(net(x_train[idx]), y_train[idx])
            loss.backward()
            opt.step()
            steps += 1
        acc = (net(x_test).argmax(1) == y_test).float().mean().item()
        print(f"[pretrain] epoch {epoch + 1}: loss {float(loss):.4f}, upright test acc {acc:.4f}")

    out = BENCH_ROOT / "artifacts" / "lora" / "pretrained"
    out.mkdir(parents=True, exist_ok=True)
    for (name, _, _), layer in zip(LAYERS, (net[0], net[2], net[4])):
        np.save(out / f"{name}_w.npy", layer.weight.detach().numpy().T.copy())  # [out,in] -> [in,out]
        np.save(out / f"{name}_b.npy", layer.bias.detach().numpy())
    rng = np.random.default_rng(2001)
    for name, n_in, n_out in LAYERS:
        np.save(out / f"{name}_lora_a.npy", (rng.standard_normal((RANK, n_out)) * 0.01).astype(np.float32))
        np.save(out / f"{name}_lora_b.npy", np.zeros((n_in, RANK), dtype=np.float32))

    # "before adaptation" on the user's rotated eval set
    ex = np.load(BENCH_ROOT / "artifacts" / "lora" / "user_eval.npy")
    ey = np.load(BENCH_ROOT / "artifacts" / "lora" / "user_eval_target.npy")
    with torch.no_grad():
        acc_rot = (net(torch.from_numpy(ex)).argmax(1) == torch.from_numpy(ey)).float().mean().item()
    print(f"[pretrain] done in {steps} steps; user rotated eval acc BEFORE adaptation: {acc_rot:.4f}")
    print(f"[pretrain] wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
