"""E1 data: two separable Gaussian blobs with structure, pre-split into batches.

The exported training-step graphs are shape-specialized (MLP: input [64,784],
Transformer: [8,64,128]), so batches are saved with exactly those shapes as
``batch_0.npy ... batch_{k-1}.npy`` under ``<out>/batches/`` plus the full set
in ``data.npz``. The loss of the exported graphs is ``mean(y*y)`` (labels are
not consumed by the graph); labels are stored for future supervised variants.

``--mnist <npz>`` swaps the blobs for MNIST images (npz with ``images``
[n,784] and ``labels``); only valid for ``--model mlp``.

Usage:
  python make_data.py --model mlp --out out/data/mlp --samples 256
  python make_data.py --model transformer --out out/data/transformer --samples 128
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

BATCH = {"mlp": 64, "transformer": 8}
DIM = {"mlp": 784, "transformer": 128}
SEQ = {"mlp": 1, "transformer": 64}  # tokens per sample


def make_blobs(model: str, samples: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    """Class c in {-1,+1}: x = c*mu*u + correlated noise; u a fixed direction."""
    rng = np.random.default_rng(seed)
    dim, seq = DIM[model], SEQ[model]
    u = rng.normal(size=dim).astype(np.float32)
    u /= np.linalg.norm(u)
    labels = (rng.random(samples) < 0.5).astype(np.int64)
    sign = np.where(labels == 0, -1.0, 1.0).astype(np.float32)
    mu = 1.5
    noise = rng.normal(size=(samples, seq, dim)).astype(np.float32)
    if seq == 1:
        # anisotropic noise: a few dominant correlated directions + isotropic floor
        basis = rng.normal(size=(8, dim)).astype(np.float32)
        basis /= np.linalg.norm(basis, axis=1, keepdims=True)
        noise = noise[:, 0, :] @ basis.T @ basis * 0.7 + noise[:, 0, :] * 0.5
        x = sign[:, None] * mu * u[None, :] + noise
    else:
        x = sign[:, None, None] * mu * u[None, None, :] + noise
        # mild per-token gain jitter so tokens are not exchangeable
        x *= rng.normal(1.0, 0.1, size=(samples, seq, 1)).astype(np.float32)
    return x.astype(np.float32), labels


def load_mnist(path: str, samples: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    data = np.load(path)
    x = data["images"].reshape(len(data["images"]), -1).astype(np.float32)
    if x.max() > 2.0:
        x /= 255.0
    x = (x - 0.5) / 0.5  # roughly zero-centered, unit-ish range
    y = data["labels"].astype(np.int64)
    rng = np.random.default_rng(seed)
    keep = rng.choice(len(x), size=min(samples, len(x)), replace=False)
    return x[keep], y[keep]


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", required=True, choices=["mlp", "transformer"])
    p.add_argument("--out", required=True, help="output dir (batches/ and data.npz)")
    p.add_argument("--samples", type=int, default=256)
    p.add_argument("--seed", type=int, default=20261002)
    p.add_argument("--mnist", default="", help="optional MNIST npz (mlp only)")
    args = p.parse_args()

    batch = BATCH[args.model]
    if args.mnist:
        if args.model != "mlp":
            raise SystemExit("--mnist is only shaped for --model mlp")
        x, y = load_mnist(args.mnist, args.samples, args.seed)
    else:
        x, y = make_blobs(args.model, args.samples, args.seed)
    usable = len(x) - len(x) % batch
    if usable == 0:
        raise SystemExit(f"need at least {batch} samples, got {len(x)}")
    x, y = x[:usable], y[:usable]

    rng = np.random.default_rng(args.seed + 1)  # seeded batch split (fixed order)
    order = rng.permutation(usable)
    x, y = x[order], y[order]

    out = Path(args.out)
    (out / "batches").mkdir(parents=True, exist_ok=True)
    for i in range(usable // batch):
        np.save(out / "batches" / f"batch_{i}.npy", x[i * batch : (i + 1) * batch])
    np.savez(out / "data.npz", x=x, y=y)
    meta = {"model": args.model, "samples": usable, "batch": batch, "seed": args.seed,
            "batches": usable // batch, "source": args.mnist or "blobs"}
    (out / "meta.json").write_text(json.dumps(meta, indent=2))
    print(f"[make_data] {args.model}: {usable} samples -> {usable // batch} batches of {batch} "
          f"in {out}/batches (x std={x.std():.3f}, mean|x|={np.abs(x).mean():.3f})")


if __name__ == "__main__":
    main()
