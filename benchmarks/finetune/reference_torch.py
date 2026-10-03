"""E1 PyTorch reference: same initial weights, same batches, same SGD (lr 0.01).

Replicates the Camel models exactly (LayerNorm eps 1e-5, GELU tanh, explicit
attention, loss ``mean(y*y)`` over all elements, update ``w - lr*grad``) so the
three-way alignment isolates kernel numerics, not semantics. Gradients come
from torch autograd; the update is written out by hand to match ``nn:sgd``.

Usage:
  python reference_torch.py --model mlp --weights ../artifacts/mlp/weights \
      --data out/data/mlp/batches --steps 100 --out out/runs/mlp/torch
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

LR = 0.01
LN_EPS = 1e-5
N_HEADS = 4


def forward(model: str, w: dict[str, torch.Tensor], x: torch.Tensor) -> torch.Tensor:
    if model == "mlp":
        h = torch.relu(x @ w["fc1_w"] + w["fc1_b"])
        h = torch.relu(h @ w["fc2_w"] + w["fc2_b"])
        return h @ w["fc3_w"] + w["fc3_b"]
    b, s, d = x.shape
    dh = d // N_HEADS

    def heads(t: torch.Tensor) -> torch.Tensor:  # [B,S,D] -> [B,H,S,Dh]
        return t.reshape(b, s, N_HEADS, dh).transpose(1, 2)

    a = F.layer_norm(x, (d,), w["ln1_gamma"], w["ln1_beta"], LN_EPS)
    q, k, v = (heads(a @ w[f"{n}_w"] + w[f"{n}_b"]) for n in ("q", "k", "v"))
    att = torch.softmax((q @ k.transpose(-2, -1)) * (1.0 / float(dh) ** 0.5), dim=-1)
    ctx = (att @ v).transpose(1, 2).reshape(b, s, d)
    h = x + ctx @ w["o_w"] + w["o_b"]
    f = F.layer_norm(h, (d,), w["ln2_gamma"], w["ln2_beta"], LN_EPS)
    f = F.gelu(f @ w["ffn1_w"] + w["ffn1_b"], approximate="tanh")
    return h + f @ w["ffn2_w"] + w["ffn2_b"]


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", required=True, choices=["mlp", "transformer"])
    p.add_argument("--weights", required=True, help="dir of initial parameter .npy files")
    p.add_argument("--data", required=True, help="dir of pre-split batch_*.npy")
    p.add_argument("--steps", type=int, default=100)
    p.add_argument("--out", required=True, help="output dir for losses.npy and trace/")
    args = p.parse_args()

    w = {
        f.stem: torch.from_numpy(np.load(f)).requires_grad_(True)
        for f in Path(args.weights).glob("*.npy")
    }
    batches = sorted(Path(args.data).glob("batch_*.npy"), key=lambda f: int(f.stem.split("_")[1]))
    out_dir = Path(args.out)
    losses = []
    for i in range(args.steps):
        x = torch.from_numpy(np.load(batches[i % len(batches)]))
        y = forward(args.model, w, x)
        loss = (y * y).mean()
        loss.backward()
        with torch.no_grad():
            for t in w.values():
                t -= LR * t.grad
                t.grad = None
        losses.append(float(loss.detach()))
        step_dir = out_dir / "trace" / f"step_{i}"
        step_dir.mkdir(parents=True, exist_ok=True)
        for k, t in w.items():
            np.save(step_dir / f"{k}.npy", t.detach().numpy())
        print(f"step={i} loss={losses[-1]:.8g}", flush=True)
    np.save(out_dir / "losses.npy", np.asarray(losses, dtype=np.float64))


if __name__ == "__main__":
    main()
