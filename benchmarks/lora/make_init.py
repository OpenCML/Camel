# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E9: build artifacts/lora/weights/ = mlp backbone (copied) + deterministic LoRA
adapter init (A ~ N(0, 0.01), B = 0 so the adapter is an exact identity at init;
rank r = 8, scale alpha/r = 2.0). Deterministic via numpy default_rng(2001).
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
RANK = 8
LAYERS = (("fc1", 784, 256), ("fc2", 256, 128), ("fc3", 128, 10))


def main() -> int:
    src = BENCH_ROOT / "artifacts" / "mlp" / "weights"
    dst = BENCH_ROOT / "artifacts" / "lora" / "weights"
    dst.mkdir(parents=True, exist_ok=True)
    for npy in src.glob("*.npy"):
        shutil.copy2(npy, dst / npy.name)
    rng = np.random.default_rng(2001)
    for name, n_in, n_out in LAYERS:
        a = (rng.standard_normal((RANK, n_out)) * 0.01).astype(np.float32)
        b = np.zeros((n_in, RANK), dtype=np.float32)
        np.save(dst / f"{name}_lora_a.npy", a)
        np.save(dst / f"{name}_lora_b.npy", b)
    shutil.copy2(BENCH_ROOT / "artifacts" / "mlp" / "input.npy", dst.parent / "input.npy")
    print(f"[init] wrote {dst} (6 backbone + 6 adapter files, rank {RANK})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
