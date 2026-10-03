"""E1 transformer real sequence data: tiny-shakespeare (char-rnn) bytes, sliced
into non-overlapping [N,64] token windows and projected to d=128 by a fixed
seeded matrix E[256,128] ~ N(0,0.2). The projection is data prep only (the
model consumes raw float tensors; there is no learned embedding anywhere in
the pipeline) and is fully determined by --seed.

Usage:
  python make_text_data.py --text <input.txt> --out out/data/transformer_text --samples 512
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

BATCH, SEQ, DIM = 8, 64, 128


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--text", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--samples", type=int, default=512)
    p.add_argument("--seed", type=int, default=5001)
    args = p.parse_args()

    raw = Path(args.text).read_bytes()
    toks = np.frombuffer(bytes(raw), dtype=np.uint8).astype(np.int64)
    rng = np.random.default_rng(args.seed)
    emb = (rng.standard_normal((256, DIM)) * 0.2).astype(np.float32)
    n = min(args.samples, len(toks) // SEQ)
    x = np.stack([emb[toks[i * SEQ : (i + 1) * SEQ]] for i in range(n)]).astype(np.float32)
    order = np.random.default_rng(args.seed + 1).permutation(n)
    x = x[order]

    out = Path(args.out)
    (out / "batches").mkdir(parents=True, exist_ok=True)
    for i in range(n // BATCH):
        np.save(out / "batches" / f"batch_{i}.npy", np.ascontiguousarray(x[i * BATCH : (i + 1) * BATCH]))
    meta = {
        "model": "transformer",
        "samples": n,
        "batch": BATCH,
        "seed": args.seed,
        "batches": n // BATCH,
        "source": "tiny-shakespeare bytes -> seeded N(0,0.2) projection E[256,128]",
    }
    (out / "meta.json").write_text(json.dumps(meta, indent=2))
    print(f"[make_text_data] {n} seqs -> {n // BATCH} batches of {BATCH} in {out}/batches (std={x.std():.3f})")


if __name__ == "__main__":
    main()
