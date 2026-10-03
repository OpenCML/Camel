"""E9 PyTorch LoRA reference: one training step of the LoRA-MLP (backbone frozen,
adapters trainable, loss mean(y*y), SGD lr 0.01) from artifacts/lora/weights,
dumping the updated parameters to --out. Mirrors lora_mlp.cml's `step` exactly;
used for the three-way (Camel native / ORT / PyTorch) alignment.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
SCALE = 2.0
LR = 0.01
LAYERS = (("fc1", "relu"), ("fc2", "relu"), ("fc3", "none"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    import torch

    wdir = BENCH_ROOT / "artifacts" / "lora" / "weights"
    w = {p.stem: torch.from_numpy(np.load(p)) for p in sorted(wdir.glob("*.npy"))}
    x = torch.from_numpy(np.load(BENCH_ROOT / "artifacts" / "lora" / "input.npy"))

    frozen = {k: v.detach() for k, v in w.items() if "lora" not in k}
    adapters = {k: v.detach().requires_grad_(True) for k, v in w.items() if "lora" in k}

    h = x
    for name, act in LAYERS:
        h = h @ frozen[f"{name}_w"] + (h @ adapters[f"{name}_lora_b"]) @ adapters[f"{name}_lora_a"] * SCALE + frozen[f"{name}_b"]
        if act == "relu":
            h = torch.relu(h)
    loss = (h * h).mean()
    loss.backward()
    with torch.no_grad():
        for k, p in adapters.items():
            p -= LR * p.grad

    args.out.mkdir(parents=True, exist_ok=True)
    for k, v in {**frozen, **adapters}.items():
        np.save(args.out / f"{k}.npy", v.detach().numpy())
    print(f"CAMEL_BENCH loss={float(loss):.9g}")
    print(f"[torch] wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
