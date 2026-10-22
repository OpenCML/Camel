# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E9 on-device personalization data: a user whose MNIST digits are rotated by a
fixed angle (+15 deg) — a small, honest distribution shift. The base model is
pretrained on upright MNIST (pretrain_torch.py); the user's device holds only
1024 labeled rotated samples for adaptation and 2000 rotated samples for eval.

Writes (deterministic, fixed seeds):
  artifacts/lora/user_batches/batch_<i>.npy   float32 [32,784] user train images
  artifacts/lora/user_batches/target_<i>.npy  float32 [32,10]  one-hot targets
  artifacts/lora/user_eval.npy                float32 [2000,784]
  artifacts/lora/user_eval_target.npy         int64   [2000]
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
DEG = 15.0
N_USER_TRAIN, BATCH, N_EVAL = 1024, 32, 2000


def rotate_batch(imgs: np.ndarray, deg: float) -> np.ndarray:
    """Nearest-neighbor rotation of [N,28,28] images by `deg` degrees."""
    theta = math.radians(deg)
    c, s = math.cos(theta), math.sin(theta)
    grid = np.arange(28, dtype=np.float64) - 13.5
    yy, xx = np.meshgrid(grid, grid, indexing="ij")
    src_x = np.clip(np.round(c * xx + s * yy + 13.5).astype(int), 0, 27)
    src_y = np.clip(np.round(-s * xx + c * yy + 13.5).astype(int), 0, 27)
    return imgs[:, src_y, src_x]


def one_hot(labels: np.ndarray, classes: int = 10) -> np.ndarray:
    out = np.zeros((labels.shape[0], classes), dtype=np.float32)
    out[np.arange(labels.shape[0]), labels] = 1.0
    return out


def main() -> int:
    data = np.load(BENCH_ROOT.parent / ".cache" / "mnist" / "mnist.npz")
    x_train = data["x_train"].astype(np.float32) / 255.0
    y_train = data["y_train"]
    x_test = data["x_test"].astype(np.float32) / 255.0
    y_test = data["y_test"]

    rng = np.random.default_rng(3001)
    utrain_idx = rng.choice(x_train.shape[0], N_USER_TRAIN, replace=False)
    ueval_idx = rng.choice(x_test.shape[0], N_EVAL, replace=False)

    ux = rotate_batch(x_train[utrain_idx].reshape(-1, 28, 28), DEG).reshape(N_USER_TRAIN, -1)
    uy = y_train[utrain_idx]
    ex = rotate_batch(x_test[ueval_idx].reshape(-1, 28, 28), DEG).reshape(N_EVAL, -1)
    ey = y_test[ueval_idx]

    out = BENCH_ROOT / "artifacts" / "lora" / "user_batches"
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.npy"):
        old.unlink()
    for i in range(N_USER_TRAIN // BATCH):
        np.save(out / f"batch_{i}.npy", np.ascontiguousarray(ux[i * BATCH : (i + 1) * BATCH], dtype=np.float32))
        np.save(out / f"target_{i}.npy", np.ascontiguousarray(one_hot(uy[i * BATCH : (i + 1) * BATCH])))
    np.save(BENCH_ROOT / "artifacts" / "lora" / "user_eval.npy", np.ascontiguousarray(ex, dtype=np.float32))
    np.save(BENCH_ROOT / "artifacts" / "lora" / "user_eval_target.npy", ey)
    print(f"[data] user train {N_USER_TRAIN} (rot {DEG} deg) -> {out}; eval {N_EVAL}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
