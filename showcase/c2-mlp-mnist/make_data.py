#!/usr/bin/env python3
"""Prepare the data and the starting weights of the C2 showcase.

Both implementations (train.cml and baseline/train_torch.py) read exactly the same files,
so a difference between their results comes from the code, not from the data or the
initialisation.

Real MNIST is used when it is available (``--source mnist`` or ``--source auto`` and a
``mnist.npz`` in the repository cache).  It is never downloaded: when it is absent the
script writes a deterministic synthetic 10-class stand-in, which is enough to exercise the
showcase machinery (the task is about the language, not the dataset).  The choice is
recorded in ``manifest.json`` and in ``dataset.id``, which the Camel program stores in its
checkpoint and checks when reading it back.

Files written to ``--out``:

  train_images.npy   (n_train, 784)  float32, pixel values in [0, 1]
  train_labels.npy   (n_train,)      int64   class indices (`F.cross_entropy` and
                                             `nn.softmax_cross_entropy` both take these)
  val_images.npy     (n_val, 784)    float32
  val_labels.npy     (n_val,)        int64
  train_order.npy    (epochs * n_train,) int64, one epoch permutation after another: the
                                             batch sampler both implementations walk, so a
                                             training step sees the same rows on both sides
  init/w1.npy        (784, hidden)   float32, He normal
  init/b1.npy        (hidden,)       float32, zeros
  init/w2.npy        (hidden, 10)    float32, He normal
  init/b2.npy        (10,)           float32, zeros
  dataset.id                         one line: the identity the checkpoint is tied to
  manifest.json                      sizes, source, seed and SHA-256 of every file
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np

SIDE = 28
FEATURES = SIDE * SIDE
CLASSES = 10
HIDDEN = 128
DEFAULT_SEED = 20261020


def find_mnist(explicit: str | None) -> Path | None:
    if explicit:
        path = Path(explicit)
        return path if path.is_file() else None
    # make_data.py -> c2-mlp-mnist -> showcase -> repository root
    root = Path(__file__).resolve().parents[2]
    for candidate in (root / ".cache" / "mnist" / "mnist.npz", Path.cwd() / ".cache" / "mnist" / "mnist.npz"):
        if candidate.is_file():
            return candidate
    return None


def load_mnist(path: Path, n_train: int, n_val: int, rng: np.random.Generator):
    data = np.load(path)
    x_train, y_train = data["x_train"], data["y_train"]
    x_val, y_val = data["x_test"], data["y_test"]
    if n_train > len(x_train) or n_val > len(x_val):
        raise SystemExit(f"MNIST holds {len(x_train)}/{len(x_val)} samples, not {n_train}/{n_val}")
    order = rng.permutation(len(x_train))[:n_train]
    val_order = rng.permutation(len(x_val))[:n_val]
    return (
        x_train[order].reshape(n_train, FEATURES).astype(np.float32) / np.float32(255.0),
        y_train[order].astype(np.int64),
        x_val[val_order].reshape(n_val, FEATURES).astype(np.float32) / np.float32(255.0),
        y_val[val_order].astype(np.int64),
        "mnist",
    )


def synthetic(n_train: int, n_val: int, rng: np.random.Generator):
    """A deterministic 10-class stand-in for MNIST.

    Every class has its own small set of bright rectangles on a noisy 28x28 grid (a crude
    digit-like stroke pattern), and every sample is that class's pattern shifted by a few
    pixels and corrupted with Gaussian noise.  It is linearly separable enough for an MLP
    to reach a high accuracy in a few hundred steps.
    """
    total = n_train + n_val
    labels = rng.integers(0, CLASSES, size=total, dtype=np.int64)

    images = rng.normal(0.05, 0.02, size=(total, SIDE, SIDE)).astype(np.float32)
    for i, label in enumerate(labels):
        shift_y, shift_x = rng.integers(-2, 3, size=2)
        for stroke in range(3):
            y0 = 4 + (label * 7 + stroke * 11) % 20 + int(shift_y)
            x0 = 3 + (label * 13 + stroke * 5) % 22 + int(shift_x)
            h = 4 + (label + stroke) % 8
            w = 3 + (label * 3 + stroke) % 7
            y0 = min(max(y0, 0), SIDE - h)
            x0 = min(max(x0, 0), SIDE - w)
            images[i, y0 : y0 + h, x0 : x0 + w] += 0.6
    images = np.clip(images, 0.0, 1.0).reshape(total, FEATURES)
    order = rng.permutation(total)
    train, val = order[:n_train], order[n_train:]
    return (
        images[train].astype(np.float32),
        labels[train],
        images[val].astype(np.float32),
        labels[val],
        "synthetic",
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="directory to write the data to")
    parser.add_argument("--train", type=int, default=1000, help="number of training samples")
    parser.add_argument("--val", type=int, default=200, help="number of validation samples")
    parser.add_argument("--hidden", type=int, default=HIDDEN, help="width of the hidden layer")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED, help="seed of the data and of the starting weights")
    parser.add_argument(
        "--source",
        choices=("auto", "mnist", "synthetic"),
        default="auto",
        help="where the images come from (auto: MNIST if the repository cache holds it, else synthetic)",
    )
    parser.add_argument("--mnist", default=None, help="path to mnist.npz")
    parser.add_argument(
        "--epochs",
        type=int,
        default=10,
        help="number of epoch permutations to write to train_order.npy",
    )
    args = parser.parse_args()

    if args.epochs < 1:
        parser.error("--epochs must be positive")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "init").mkdir(exist_ok=True)

    # Three independent streams: the data does not move when the initialisation or the
    # batch sampler changes.
    data_rng = np.random.default_rng(args.seed)
    init_rng = np.random.default_rng(args.seed + 1)
    order_rng = np.random.default_rng(args.seed + 2)

    mnist = find_mnist(args.mnist) if args.source in ("auto", "mnist") else None
    if args.source == "mnist" and mnist is None:
        raise SystemExit("--source mnist was asked for but no mnist.npz was found")
    if mnist is not None:
        train_x, train_y, val_x, val_y, source = load_mnist(mnist, args.train, args.val, data_rng)
    else:
        train_x, train_y, val_x, val_y, source = synthetic(args.train, args.val, data_rng)

    hidden = args.hidden
    init = {
        "w1": init_rng.normal(0.0, np.sqrt(2.0 / FEATURES), size=(FEATURES, hidden)).astype(np.float32),
        "b1": np.zeros(hidden, dtype=np.float32),
        "w2": init_rng.normal(0.0, np.sqrt(2.0 / hidden), size=(hidden, CLASSES)).astype(np.float32),
        "b2": np.zeros(CLASSES, dtype=np.float32),
    }

    files = {
        "train_images.npy": train_x,
        "train_labels.npy": train_y,
        "val_images.npy": val_x,
        "val_labels.npy": val_y,
        # One epoch permutation per row-major block, written flat so a step can slice it.
        "train_order.npy": np.concatenate(
            [order_rng.permutation(args.train) for _ in range(args.epochs)]
        ).astype(np.int64),
    }
    for name, array in files.items():
        np.save(out / name, array)
    for name, array in init.items():
        np.save(out / "init" / f"{name}.npy", array)

    # A content identity: regenerating the same data gives the same id, so a checkpoint
    # written for different data fails to load (tree.save_tree checks it). The batch order
    # is not part of it: it changes with the number of epochs, and a checkpoint stays valid.
    fingerprint = hashlib.sha256()
    for name in ("train_images.npy", "train_labels.npy", "val_images.npy", "val_labels.npy"):
        fingerprint.update((out / name).read_bytes())
    digest = fingerprint.hexdigest()[:16]
    dataset_id = f"c2-{source}-train{args.train}-val{args.val}-hidden{hidden}-{digest}"
    (out / "dataset.id").write_text(dataset_id, encoding="utf-8")

    manifest = {
        "dataset_id": dataset_id,
        "source": source,
        "mnist_file": str(mnist) if mnist is not None else None,
        "generator": "make_data.py",
        "generator_note": (
            "real MNIST, shuffled with seed and normalised to [0, 1]"
            if source == "mnist"
            else "deterministic synthetic stand-in; see the module docstring of make_data.py"
        ),
        "seed": args.seed,
        "train": args.train,
        "val": args.val,
        "hidden": hidden,
        "epochs": args.epochs,
        "features": FEATURES,
        "classes": CLASSES,
        "files": {
            name: {"shape": list(np.load(out / name).shape), "sha256": sha256(out / name)}
            for name in files
        },
        "init": {
            name: {"shape": list(array.shape), "sha256": sha256(out / "init" / f"{name}.npy")}
            for name, array in init.items()
        },
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"data   {out} ({source}, train={args.train}, val={args.val}, hidden={hidden})")
    print(f"id     {dataset_id}")
    print(f"init   w1{tuple(init['w1'].shape)} b1{tuple(init['b1'].shape)} "
          f"w2{tuple(init['w2'].shape)} b2{tuple(init['b2'].shape)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())