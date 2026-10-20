# D5 -- the PyTorch counterpart of train.cml.
#
# The same recursive binary Tree-LSTM, the same initial weights, one sample per step and the
# same Adam hyperparameters.  PyTorch's autograd handles the recursion the same way the Camel
# macro does.

import argparse
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


# == model ==
class TreeLstm(nn.Module):
    def __init__(self, init: dict[str, np.ndarray]):
        super().__init__()
        self.emb = nn.Parameter(torch.from_numpy(init["emb"]))
        for name in ("wi", "wfl", "wfr", "wo", "wu",
                     "ui", "ufl", "ufr", "uo", "uu",
                     "bi", "bfl", "bfr", "bo", "bu",
                     "wout", "bout"):
            self.register_parameter(name, nn.Parameter(torch.from_numpy(init[name])))

    def cell(self, x, hl, cl, hr, cr):
        i = torch.sigmoid(x @ self.wi + hl @ self.ui + hr @ self.ui + self.bi)
        fl = torch.sigmoid(x @ self.wfl + hl @ self.ufl + hr @ self.ufl + self.bfl)
        fr = torch.sigmoid(x @ self.wfr + hl @ self.ufr + hr @ self.ufr + self.bfr)
        o = torch.sigmoid(x @ self.wo + hl @ self.uo + hr @ self.uo + self.bo)
        u = torch.tanh(x @ self.wu + hl @ self.uu + hr @ self.uu + self.bu)
        c = i * u + fl * cl + fr * cr
        return o * torch.tanh(c), c

    def forward(self, tree):
        if isinstance(tree, torch.Tensor):
            empty = torch.zeros(1, self.emb.shape[1])
            return self.cell(tree @ self.emb, empty, empty, empty, empty)
        left, right = tree
        hl, cl = self.forward(left)
        hr, cr = self.forward(right)
        return self.cell(hl + hr, hl, cl, hr, cr)


# == data ==
def build(onehot: torch.Tensor):
    return ((onehot[0:1], onehot[1:2]), (onehot[2:3], onehot[3:4]))


# == evaluate ==
def accuracy(model: TreeLstm, words: torch.Tensor, ids: torch.Tensor) -> float:
    correct = 0
    with torch.no_grad():
        for index in range(words.shape[0]):
            tree = build(words[index])
            hidden, state = model(tree)
            logits = hidden @ model.wout + model.bout
            correct += int(logits.argmax(dim=1).item() == ids[index].item())
    return correct / words.shape[0]


# == entry ==
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--steps", type=int, default=300)
    parser.add_argument("--lr", type=float, default=0.01)
    parser.add_argument("--log_every", type=int, default=50)
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args()

    torch.set_num_threads(args.threads)
    data = Path(args.data)
    train_words = torch.from_numpy(np.load(data / "train_words.npy"))
    train_ids = torch.from_numpy(np.load(data / "train_ids.npy")).long()
    val_words = torch.from_numpy(np.load(data / "val_words.npy"))
    val_ids = torch.from_numpy(np.load(data / "val_ids.npy")).long()
    init = {p.stem: np.load(p) for p in (data / "init").glob("*.npy")}
    model = TreeLstm(init)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)

    # == train ==
    params = sum(p.numel() for p in model.parameters())
    train_n = train_words.shape[0]
    before = accuracy(model, val_words, val_ids)
    print(f"d5 config steps={args.steps} lr={args.lr} threads={args.threads} "
          f"params={params} train={train_n}", flush=True)
    print(f"d5 init val_accuracy={before:.9f}", flush=True)

    started = time.perf_counter()
    for step in range(args.steps):
        index = step % train_n
        tree = build(train_words[index])
        hidden, state = model(tree)
        logits = hidden @ model.wout + model.bout
        value = F.cross_entropy(logits, train_ids[index:index + 1])
        if step % args.log_every == 0:
            print(f"d5 step={step} loss={value.item():.9f}", flush=True)
        optimizer.zero_grad(set_to_none=True)
        value.backward()
        optimizer.step()
    elapsed = time.perf_counter() - started

    # == evaluate ==
    after = accuracy(model, val_words, val_ids)
    print(f"d5 trained steps={args.steps} seconds={elapsed:.6f} "
          f"val_accuracy={after:.9f}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())