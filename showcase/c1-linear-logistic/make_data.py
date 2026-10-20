# C1 -- deterministic data for the linear and the logistic regression examples.
#
# There is no download and no dependency beyond NumPy: both problems are synthetic, with a
# fixed seed, so the Camel program and the PyTorch baseline read byte-identical arrays and the
# only difference between the two runs is the implementation under comparison.

import argparse
import json
from pathlib import Path

import numpy as np


def linear(rng, n: int, noise: float):
    x = rng.uniform(-2.0, 2.0, size=(n, 2)).astype(np.float32)
    y = (2.0 * x[:, 0] - 1.5 * x[:, 1] + 0.5).astype(np.float32)
    y = (y + rng.normal(0.0, noise, size=n).astype(np.float32)).reshape(n, 1)
    return x, y


def logistic(rng, n: int):
    # Two Gaussian clusters whose Bayes boundary is a line; labels are 0/1.
    half = n // 2
    left = rng.normal(loc=(-1.0, -1.0), scale=0.7, size=(half, 2))
    right = rng.normal(loc=(1.2, 1.2), scale=0.7, size=(n - half, 2))
    x = np.concatenate([left, right]).astype(np.float32)
    labels = np.concatenate([np.zeros(half), np.ones(n - half)]).astype(np.float32)
    order = rng.permutation(n)
    return x[order], labels.reshape(n, 1)[order]


def save(directory: Path, arrays: dict[str, np.ndarray]):
    directory.mkdir(parents=True, exist_ok=True)
    for name, array in arrays.items():
        np.save(directory / f"{name}.npy", np.asarray(array, dtype=np.float32))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    parser.add_argument("--train", type=int, default=512)
    parser.add_argument("--val", type=int, default=128)
    parser.add_argument("--seed", type=int, default=20261020)
    parser.add_argument("--noise", type=float, default=0.05)
    args = parser.parse_args()

    out = Path(args.out)
    rng = np.random.default_rng(args.seed)

    for name, make in (("linear", lambda n: linear(rng, n, args.noise)),
                       ("logistic", lambda n: logistic(rng, n))):
        train_x, train_y = make(args.train)
        val_x, val_y = make(args.val)
        save(out / name, {
            "train_x": train_x, "train_y": train_y,
            "val_x": val_x, "val_y": val_y,
        })
        # Both models start at zero: the first loss is fully determined by the data, and the
        # first gradient step is therefore comparable without a separate initialization file.
        save(out / name / "init", {
            "w": np.zeros((2, 1), dtype=np.float32),
            "b": np.zeros((1,), dtype=np.float32),
        })

    (out / "meta.json").write_text(json.dumps({
        "seed": args.seed,
        "train": args.train,
        "val": args.val,
        "noise": args.noise,
        "models": ["linear", "logistic"],
    }, indent=2) + "\n", encoding="utf-8")
    print(f"c1 data: train={args.train} val={args.val} seed={args.seed} out={out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())