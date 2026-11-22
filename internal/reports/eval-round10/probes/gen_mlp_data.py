"""Write the c2-mlp-mnist data files without numpy.

`showcase/c2-mlp-mnist/make_data.py` needs numpy, and the measurement host has none (the pip
install stalls with no network). The program under test does not care where the numbers come from,
only that the files have the shapes `make_data.py` documents, so this writes the same set from a
fixed seed with the standard library alone:

  train_images.npy (n,784) f32   train_labels.npy (n,) i64
  val_images.npy   (m,784) f32   val_labels.npy   (m,) i64
  train_order.npy  (epochs*n,) i64
  init/w1.npy (784,hidden) f32   init/b1.npy (hidden,) f32
  init/w2.npy (hidden,10) f32    init/b2.npy (10,) f32
  dataset.id, manifest.json

  python3 gen_mlp_data.py --out <dir> [--train 64] [--val 16] [--hidden 128] [--epochs 2]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import struct
from pathlib import Path

FEATURES = 784
CLASSES = 10


def npy_bytes(descr: str, shape: tuple[int, ...], payload: bytes) -> bytes:
    header = "{'descr': '%s', 'fortran_order': False, 'shape': %s, }" % (descr, shape if len(shape) > 1 else f"({shape[0]},)")
    pad = 64 - (len(header) + 1 + 10) % 64
    header = header + " " * (pad % 64) + "\n"
    return b"\x93NUMPY\x01\x00" + struct.pack("<H", len(header.encode())) + header.encode() + payload


def f32(values) -> bytes:
    return struct.pack("<%df" % len(values), *values)


def i64(values) -> bytes:
    return struct.pack("<%dq" % len(values), *values)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--train", type=int, default=64)
    ap.add_argument("--val", type=int, default=16)
    ap.add_argument("--hidden", type=int, default=128)
    ap.add_argument("--epochs", type=int, default=2)
    ap.add_argument("--seed", type=int, default=20261020)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "init").mkdir(exist_ok=True)

    def images(n: int):
        return [rng.random() for _ in range(n * FEATURES)]

    def labels(n: int):
        return [i % CLASSES for i in range(n)]

    x_tr, y_tr = images(a.train), labels(a.train)
    x_va, y_va = images(a.val), labels(a.val)

    order = []
    for _ in range(a.epochs):
        epoch = list(range(a.train))
        rng.shuffle(epoch)
        order.extend(epoch)

    scale1 = math.sqrt(2.0 / FEATURES)
    scale2 = math.sqrt(2.0 / a.hidden)
    w1 = [rng.gauss(0.0, scale1) for _ in range(FEATURES * a.hidden)]
    w2 = [rng.gauss(0.0, scale2) for _ in range(a.hidden * CLASSES)]

    files = {
        "train_images.npy": npy_bytes("<f4", (a.train, FEATURES), f32(x_tr)),
        "train_labels.npy": npy_bytes("<i8", (a.train,), i64(y_tr)),
        "val_images.npy": npy_bytes("<f4", (a.val, FEATURES), f32(x_va)),
        "val_labels.npy": npy_bytes("<i8", (a.val,), i64(y_va)),
        "train_order.npy": npy_bytes("<i8", (len(order),), i64(order)),
        "init/w1.npy": npy_bytes("<f4", (FEATURES, a.hidden), f32(w1)),
        "init/b1.npy": npy_bytes("<f4", (a.hidden,), f32([0.0] * a.hidden)),
        "init/w2.npy": npy_bytes("<f4", (a.hidden, CLASSES), f32(w2)),
        "init/b2.npy": npy_bytes("<f4", (CLASSES,), f32([0.0] * CLASSES)),
    }
    manifest = {}
    for name, data in files.items():
        (out / name).write_bytes(data)
        manifest[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    (out / "dataset.id").write_text("synthetic-no-numpy-pin-2d7c5c3a1\n")
    (out / "manifest.json").write_text(json.dumps({"source": "synthetic", "seed": a.seed, "files": manifest}, indent=1) + "\n")
    print("wrote", out, "train", a.train, "val", a.val, "hidden", a.hidden, "order", len(order))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())