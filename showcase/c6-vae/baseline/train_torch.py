# C6 -- the PyTorch counterpart of train.cml.
#
# The same ten tensors, the same initial values, the same pre-generated epsilons and the same
# Adam hyperparameters.  The only difference is the implementation.

import argparse
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


# == model ==
class Vae(nn.Module):
    def __init__(self, init: dict[str, np.ndarray]):
        super().__init__()
        h = init["ew1"].shape[1]
        self.ew1 = nn.Linear(2, h)
        self.ewm = nn.Linear(h, 2)
        self.ewv = nn.Linear(h, 2)
        self.dw1 = nn.Linear(2, h)
        self.dw2 = nn.Linear(h, 2)
        with torch.no_grad():
            biases = {"ew1": "eb1", "ewm": "ebm", "ewv": "ebv", "dw1": "db1", "dw2": "db2"}
            for name, bias in biases.items():
                getattr(self, name).weight.copy_(torch.from_numpy(init[name].T.copy()))
                getattr(self, name).bias.copy_(torch.from_numpy(init[bias].copy()))

    def encode(self, x):
        hidden = F.relu(self.ew1(x))
        return self.ewm(hidden), self.ewv(hidden)

    def decode(self, z):
        return self.dw2(F.relu(self.dw1(z)))


# == loss ==
def loss_parts(model: Vae, x, eps):
    mu, logvar = model.encode(x)
    z = mu + torch.exp(0.5 * logvar) * eps
    errors = model.decode(z) - x
    reconstruction = (errors * errors).sum(dim=1).mean()
    kl = -0.5 * (1.0 + logvar - mu * mu - torch.exp(logvar)).sum(dim=1).mean()
    return reconstruction, kl


# == data ==
def load(data: Path):
    init = {p.stem: np.load(p) for p in (data / "init").glob("*.npy")}
    return (
        torch.from_numpy(np.load(data / "train_x.npy")),
        torch.from_numpy(np.load(data / "val_x.npy")),
        torch.from_numpy(np.load(data / "noise.npy")),
        init,
    )


# == evaluate ==
def evaluate(model: Vae, x):
    with torch.no_grad():
        mu, logvar = model.encode(x)
        errors = model.decode(mu) - x
        reconstruction = (errors * errors).sum(dim=1).mean().item()
        kl = (-0.5 * (1.0 + logvar - mu * mu - torch.exp(logvar)).sum(dim=1).mean()).item()
    return reconstruction, kl, reconstruction + kl


# == entry ==
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--steps", type=int, default=50)
    parser.add_argument("--batch", type=int, default=32)
    parser.add_argument("--lr", type=float, default=0.005)
    parser.add_argument("--log_every", type=int, default=10)
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args()

    torch.set_num_threads(args.threads)
    data = Path(args.data)
    train_x, val_x, noise, init = load(data)
    model = Vae(init)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)

    # == train ==
    params = sum(p.numel() for p in model.parameters())
    batches = train_x.shape[0] // args.batch
    before = evaluate(model, val_x)
    print(f"c6 config steps={args.steps} batch={args.batch} lr={args.lr} "
          f"threads={args.threads} params={params} batches={batches}", flush=True)
    print(f"c6 init val_loss={before[2]:.9f} recon={before[0]:.9f} kl={before[1]:.9f}", flush=True)

    started = time.perf_counter()
    for step in range(args.steps):
        start = (step % batches) * args.batch
        x = train_x[start:start + args.batch]
        eps = noise[step * args.batch:step * args.batch + args.batch]
        optimizer.zero_grad(set_to_none=True)
        reconstruction, kl = loss_parts(model, x, eps)
        value = reconstruction + kl
        if step % args.log_every == 0:
            print(f"c6 step={step} loss={value.item():.9f}", flush=True)
        value.backward()
        optimizer.step()
    elapsed = time.perf_counter() - started

    # == evaluate ==
    after = evaluate(model, val_x)
    print(f"c6 trained steps={args.steps} seconds={elapsed:.6f} "
          f"val_loss={after[2]:.9f} recon={after[0]:.9f} kl={after[1]:.9f}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())