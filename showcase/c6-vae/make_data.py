# C6 -- deterministic data for the 2-D variational autoencoder.
#
# The data is a mixture of four Gaussian clusters in the plane; the noise for the
# reparameterization is generated here, not inside either implementation, so both sides see the
# same epsilons and the comparison isolates the implementation.  Keyed random numbers (H20-3)
# will replace the pre-generated file once they land.

import argparse
import json
from pathlib import Path

import numpy as np


def sample(rng, n: int):
    centres = np.array([[-2.0, -1.0], [1.5, 2.0], [-1.5, 2.0], [2.0, -2.0]], dtype=np.float32)
    index = rng.integers(0, len(centres), size=n)
    x = centres[index] + rng.normal(0.0, 0.35, size=(n, 2)).astype(np.float32)
    return x.astype(np.float32), index.astype(np.int64)


def save(directory: Path, arrays: dict[str, np.ndarray]):
    directory.mkdir(parents=True, exist_ok=True)
    for name, array in arrays.items():
        np.save(directory / f"{name}.npy", np.asarray(array, dtype=np.float32))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    parser.add_argument("--train", type=int, default=1024)
    parser.add_argument("--val", type=int, default=256)
    parser.add_argument("--hidden", type=int, default=16)
    parser.add_argument("--noise", type=int, default=40000)
    parser.add_argument("--seed", type=int, default=20261020)
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)
    out = Path(args.out)
    train_x, train_cluster = sample(rng, args.train)
    val_x, val_cluster = sample(rng, args.val)
    eps = rng.normal(0.0, 1.0, size=(args.noise, 2)).astype(np.float32)

    def weights(rows: int, cols: int, scale: float):
        return (rng.normal(0.0, scale, size=(rows, cols))).astype(np.float32)

    save(out, {"train_x": train_x, "val_x": val_x, "noise": eps,
               "train_cluster": train_cluster, "val_cluster": val_cluster})
    h = args.hidden
    save(out / "init", {
        "ew1": weights(2, h, 0.5), "eb1": np.zeros(h, dtype=np.float32),
        "ewm": weights(h, 2, 0.3), "ebm": np.zeros(2, dtype=np.float32),
        "ewv": weights(h, 2, 0.3), "ebv": np.zeros(2, dtype=np.float32),
        "dw1": weights(2, h, 0.5), "db1": np.zeros(h, dtype=np.float32),
        "dw2": weights(h, 2, 0.3), "db2": np.zeros(2, dtype=np.float32),
    })
    (out / "meta.json").write_text(json.dumps({
        "seed": args.seed, "train": args.train, "val": args.val,
        "hidden": h, "noise": args.noise,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"c6 data: train={args.train} val={args.val} hidden={h} noise={args.noise}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())