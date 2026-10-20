# D5 -- deterministic data for the Tree-LSTM sentiment example.
#
# Every sentence is a fixed complete binary tree with four leaves.  A word id below 4 is
# negative, 4 and above is positive; the label is the XOR of the left and right subtree
# polarity, so it depends on the tree grouping rather than on a bag of words.  The tree shape is
# fixed so the example can concentrate on the recursive Tree-LSTM and on batching/recursion
# semantics, not on parsing variable-length trees.

import argparse
import json
from pathlib import Path

import numpy as np

VOCAB = 8
HIDDEN = 8


def save(directory: Path, arrays: dict[str, np.ndarray]):
    directory.mkdir(parents=True, exist_ok=True)
    for name, array in arrays.items():
        np.save(directory / f"{name}.npy", np.asarray(array, dtype=np.float32))


def make(rng, n: int):
    words = rng.integers(0, VOCAB, size=(n, 4))
    polarity = np.where(words < VOCAB // 2, -1, 1)
    # The label is the XOR of the two subtrees' polarity: a bag of words cannot compute it,
    # because the same four polarities give different labels depending on how they are grouped.
    left = polarity[:, :2].sum(axis=1) > 0
    right = polarity[:, 2:].sum(axis=1) > 0
    ids = (left != right).astype(np.int64)
    onehot = np.zeros((n, 4, VOCAB), dtype=np.float32)
    for sample in range(n):
        for position in range(4):
            onehot[sample, position, words[sample, position]] = 1.0
    labels = np.zeros((n, 2), dtype=np.float32)
    labels[np.arange(n), ids] = 1.0
    return onehot, labels, ids


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    parser.add_argument("--train", type=int, default=256)
    parser.add_argument("--val", type=int, default=64)
    parser.add_argument("--seed", type=int, default=20261020)
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)
    out = Path(args.out)
    train_words, train_labels, train_ids = make(rng, args.train)
    val_words, val_labels, val_ids = make(rng, args.val)
    save(out, {
        "train_words": train_words, "train_labels": train_labels, "train_ids": train_ids,
        "val_words": val_words, "val_labels": val_labels, "val_ids": val_ids,
    })

    def w(rows, cols):
        return rng.normal(0.0, 1.0 / np.sqrt(max(rows, 1)), size=(rows, cols)).astype(np.float32)

    init = {
        "emb": rng.normal(0.0, 0.3, size=(VOCAB, HIDDEN)).astype(np.float32),
        "wi": w(HIDDEN, HIDDEN), "wfl": w(HIDDEN, HIDDEN), "wfr": w(HIDDEN, HIDDEN),
        "wo": w(HIDDEN, HIDDEN), "wu": w(HIDDEN, HIDDEN),
        "ui": w(HIDDEN, HIDDEN), "ufl": w(HIDDEN, HIDDEN), "ufr": w(HIDDEN, HIDDEN),
        "uo": w(HIDDEN, HIDDEN), "uu": w(HIDDEN, HIDDEN),
        "bi": np.zeros(HIDDEN), "bfl": np.zeros(HIDDEN), "bfr": np.zeros(HIDDEN),
        "bo": np.zeros(HIDDEN), "bu": np.zeros(HIDDEN),
        "wout": rng.normal(0.0, 0.3, size=(HIDDEN, 2)).astype(np.float32),
        "bout": np.zeros(2),
    }
    save(out / "init", init)
    (out / "meta.json").write_text(json.dumps({
        "seed": args.seed, "train": args.train, "val": args.val,
        "vocab": VOCAB, "hidden": HIDDEN, "leaves": 4,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"d5 data: train={args.train} val={args.val} vocab={VOCAB} hidden={HIDDEN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())