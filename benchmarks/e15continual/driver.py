# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E15/T11 on-device CONTINUAL learning driver (MLSys 2027 eval).

Five "users" arrive in order — MNIST sub-distributions rotated by
{0, +15, +30, +45, -15} deg (make_users.py). ONE exported LoRA training-step
graph (benchmarks/lora/out/lora_ce_step_lr01.onnx, lr 0.1, backbone frozen,
rank 8) fine-tunes sequentially: 640 steps (20 epochs x 32 batches of 32) per
user; the adapter state after user k is the starting point for user k+1 — the
adapter is NOT reset between users (continual). Init: artifacts/lora/pretrained
(E9 backbone + zero-identity adapters).

Three parties run the SAME protocol (same lr, same batches, same init):
  ort           this file drives the exported graph in an unmodified ONNX
                Runtime InferenceSession, feeding updated adapters back
  camel-native  lora_mlp.cml train_ce per user (subprocess; weights chained
                through per-user dump directories)
  torch         a plain PyTorch LoRA reference loop

After each user the adapter state is evaluated on EVERY user's eval set
(shared torch forward, 2000 samples each) -> the 5x5 final-accuracy matrix
(rows: after user k; columns: eval on user j) per party, the forgetting
curves (acc on user j as training moves past it), and the three-way
consistency summary.

Scope disclaimer: an APPLICATION-SCENARIO demo of one artifact serving
sequential on-device fine-tuning; no anti-forgetting algorithm novelty is
claimed (the forgetting is the phenomenon, not the contribution).

Outputs (benchmarks/e15continual/out/):
  accuracy_matrix_<party>.csv  rows init/after_user_0..4 x cols eval_user_0..4
  losses.csv                   step, user, loss per party
  drift.csv                    per user: worst pairwise |dloss| and max|dW|
  forgetting_curves.png        acc on user j after each user k >= j, per party
  summary.txt                  headline numbers + consistency verdict
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "e15continual"
A_LORA = ROOT / "artifacts" / "lora"
USERS = ROOT / "artifacts" / "e15" / "users"
OUT = HERE / "out"
GRAPH = ROOT / "e15continual" / "out" / "lora_ce_step_lr005.onnx"
NATIVE_CML = ROOT / "e15continual" / "lora_ce_step_lr.cml"
ANGLES = [0.0, 15.0, 30.0, 45.0, -15.0]
STEPS, NB = 640, 32
PARAM_NAMES = [
    "fc1_w", "fc1_b", "fc1_lora_a", "fc1_lora_b",
    "fc2_w", "fc2_b", "fc2_lora_a", "fc2_lora_b",
    "fc3_w", "fc3_b", "fc3_lora_a", "fc3_lora_b",
]
ADAPTERS = [n for n in PARAM_NAMES if "lora" in n]


def load_init() -> dict:
    return {n: np.load(A_LORA / "pretrained" / f"{n}.npy") for n in PARAM_NAMES}


def batches(k: int, i: int):
    d = USERS / f"user_{k}"
    return np.load(d / f"batch_{i % NB}.npy"), np.load(d / f"target_{i % NB}.npy")


def accuracy(w: dict, k: int) -> float:
    """Shared eval for all parties: LoRA forward on user k's eval set (torch)."""
    import torch

    x = torch.from_numpy(np.load(USERS / f"user_{k}" / "eval.npy"))
    y = np.load(USERS / f"user_{k}" / "eval_target.npy")
    h = x
    for n in ("fc1", "fc2", "fc3"):
        h = h @ torch.from_numpy(w[f"{n}_w"]) + (h @ torch.from_numpy(w[f"{n}_lora_b"])) @ torch.from_numpy(
            w[f"{n}_lora_a"]
        ) * 2.0 + torch.from_numpy(w[f"{n}_b"])
        if n != "fc3":
            h = torch.relu(h)
    return float((h.argmax(1).numpy() == y).mean())


def eval_all(w: dict, n_users: int) -> list:
    return [accuracy(w, j) for j in range(n_users)]


def run_ort_user(sess, outs: list, w: dict, k: int, steps: int) -> tuple[dict, list]:
    ins = [i.name for i in sess.get_inputs()]
    log = []
    for i in range(steps):
        x, t = batches(k, i)
        feeds = {n: w[n] for n in ins if n not in ("input1", "input2")}
        feeds["input1"], feeds["input2"] = x, t
        res = dict(zip(outs, sess.run(None, feeds)))
        w = {n: res[f"output_1_{n}"] for n in PARAM_NAMES}
        log.append(float(res["output_0"]))
    return w, log


def run_torch_user(w0: dict, k: int, steps: int, lr: float) -> tuple[dict, list]:
    import torch

    adapters = {n: torch.from_numpy(w0[n]).requires_grad_(True) for n in ADAPTERS}
    frozen = {n: torch.from_numpy(w0[n]) for n in PARAM_NAMES if n not in ADAPTERS}
    log = []
    for i in range(steps):
        x, t = batches(k, i)
        xt, tt = torch.from_numpy(x), torch.from_numpy(t)
        h = xt
        for n in ("fc1", "fc2", "fc3"):
            h = h @ frozen[f"{n}_w"] + (h @ adapters[f"{n}_lora_b"]) @ adapters[f"{n}_lora_a"] * 2.0 + frozen[f"{n}_b"]
            if n != "fc3":
                h = torch.relu(h)
        loss = torch.nn.functional.cross_entropy(h, tt.argmax(1))
        loss.backward()
        with torch.no_grad():
            for p in adapters.values():
                p -= lr * p.grad
                p.grad = None
        log.append(float(loss.detach()))
    w = {n: (adapters[n].detach().numpy() if n in adapters else w0[n]) for n in PARAM_NAMES}
    return w, log


def run_native_user(k: int, w_in: Path, w_out: Path, steps: int, lr: float) -> list:
    import os
    import re

    env = dict(os.environ)
    env.update(
        {
            "CAMEL_HOME": str(ROOT.parent / "out" / "latest"),
            "CAMEL_PACKAGES": str(ROOT / "common"),
            "CAMEL_BENCH_WEIGHTS": str(w_in),
            "CAMEL_BENCH_INPUT": str(A_LORA / "input.npy"),
            "CAMEL_FT_BATCHES": str(USERS / f"user_{k}"),
            "CAMEL_FT_STEPS": str(steps),
            "CAMEL_FT_NBATCHES": str(NB),
            "CAMEL_FT_DUMP": str(w_out),
            "CAMEL_FT_LR": str(lr),
            "CAMEL_BENCH_WORKLOAD": "train_ce",
        }
    )
    proc = subprocess.run(
        [str(ROOT.parent / "out" / "latest" / "bin" / "camel.exe"), str(NATIVE_CML)],
        capture_output=True,
        text=True,
        env=env,
        cwd=ROOT / "lora",
    )
    if proc.returncode != 0:
        raise RuntimeError(f"native run failed on user {k}: {proc.stderr[-2000:]}")
    losses = {int(m.group(1)): float(m.group(2)) for m in re.finditer(r"step=(\d+) loss=(nan|[0-9.eE+-]+)", proc.stdout)}
    return [losses.get(i, float("nan")) for i in range(steps)]


def main() -> int:
    global OUT
    ap = argparse.ArgumentParser()
    ap.add_argument("--users", type=int, default=5, help="number of users (prefix of the 5)")
    ap.add_argument("--steps", type=int, default=STEPS, help="steps per user")
    ap.add_argument("--lr", type=float, default=0.05, help="SGD lr baked into the exported graph")
    ap.add_argument("--graph", type=Path, default=GRAPH, help="the exported training-step graph")
    ap.add_argument("--out", type=Path, default=OUT, help="output directory")
    args = ap.parse_args()
    n_users, steps, lr = args.users, args.steps, args.lr
    OUT = args.out

    OUT.mkdir(parents=True, exist_ok=True)
    import onnxruntime as ort

    sess = ort.InferenceSession(str(args.graph), providers=["CPUExecutionProvider"])
    outs = [o.name for o in sess.get_outputs()]

    states = {"ort": load_init(), "torch": load_init()}
    native_in = A_LORA / "pretrained"
    losses = {p: [] for p in ("ort", "torch", "native")}
    matrices = {p: [eval_all(states[p], n_users)] for p in ("ort", "torch")}
    matrices["native"] = [eval_all(load_init(), n_users)]
    drift_rows = []

    for k in range(n_users):
        print(f"[user {k} | rot {ANGLES[k]} deg] {steps} steps x3 parties (lr {lr}) ...")
        states["ort"], lo = run_ort_user(sess, outs, states["ort"], k, steps)
        states["torch"], lt = run_torch_user(states["torch"], k, steps, lr)
        native_out = OUT / "native_state" / f"after_user_{k}"
        ln = run_native_user(k, native_in, native_out, steps, lr)
        native_in = native_out
        states["native"] = {n: np.load(native_out / f"{n}.npy") for n in PARAM_NAMES}
        for p, ls in (("ort", lo), ("torch", lt), ("native", ln)):
            losses[p].extend(ls)
        nan_parties = [p for p, ls in (("ort", lo), ("torch", lt), ("native", ln)) if any(np.isnan(ls))]
        if nan_parties:
            first_bad = {p: next(i for i, v in enumerate(ls) if np.isnan(v)) for p, ls in (("ort", lo), ("torch", lt), ("native", ln)) if any(np.isnan(ls))}
            print(f"  UNSTABLE: loss went NaN on user {k} for {nan_parties} (first-bad-step {first_bad})")

        with np.errstate(invalid="ignore"):
            diffs = [np.abs(np.array(a) - np.array(b)) for a, b in ((lo, ln), (lo, lt), (ln, lt))]
        d_on, d_ot, d_nt = (float(np.nanmax(d)) for d in diffs)
        dw = max(
            float(np.max(np.abs(states[a][n] - states[b][n])))
            for n in ADAPTERS
            for a, b in (("ort", "native"), ("ort", "torch"), ("native", "torch"))
        )
        drift_rows.append((k, d_on, d_ot, d_nt, dw))
        for p in ("ort", "torch", "native"):
            matrices[p].append(eval_all(states[p], n_users))
            snap = OUT / "state" / p / f"after_user_{k}"
            snap.mkdir(parents=True, exist_ok=True)
            for n in ADAPTERS:
                np.save(snap / f"{n}.npy", states[p][n])
        print(f"  drift: max|dloss| ort/nat {d_on:.2e} ort/tor {d_ot:.2e} nat/tor {d_nt:.2e}  max|dW| {dw:.2e}")
        print(f"  acc after user {k}: " + "  ".join(f"{p} " + "/".join(f"{a:.4f}" for a in matrices[p][-1]) for p in ("ort", "torch", "native")))

    with (OUT / "losses.csv").open("w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["user", "step", "loss_ort", "loss_torch", "loss_native"])
        for k in range(n_users):
            for i in range(steps):
                base = k * steps + i
                wcsv.writerow([k, i, losses["ort"][base], losses["torch"][base], losses["native"][base]])
    with (OUT / "drift.csv").open("w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["user", "max_dloss_ort_native", "max_dloss_ort_torch", "max_dloss_native_torch", "max_dW_adapters"])
        wcsv.writerows(drift_rows)

    row_labels = ["init"] + [f"after_user_{k}" for k in range(n_users)]
    for p, mat in matrices.items():
        with (OUT / f"accuracy_matrix_{p}.csv").open("w", newline="") as f:
            wcsv = csv.writer(f)
            wcsv.writerow(["state"] + [f"eval_user_{j}" for j in range(n_users)])
            for label, row in zip(row_labels, mat):
                wcsv.writerow([label] + [f"{a:.6f}" for a in row])

    # forgetting curves: one subplot per party, one line per user j (k >= j)
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.2), sharey=True)
    for ax, p in zip(axes, ("ort", "native", "torch")):
        for j in range(n_users):
            ks = list(range(j, n_users))
            ax.plot(ks, [matrices[p][k + 1][j] for k in ks], marker="o", ms=3, label=f"user {j} ({ANGLES[j]:g} deg)")
        ax.set_title(p)
        ax.set_xlabel("after training user k")
        ax.set_xticks(range(n_users))
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("eval accuracy on user j")
    axes[-1].legend(fontsize=7, loc="lower left")
    fig.suptitle("E15 continual LoRA fine-tune: accuracy on user j as training moves past it")
    fig.tight_layout()
    fig.savefig(OUT / "forgetting_curves.png", dpi=150)

    worst_acc = max(
        abs(matrices[a][r][j] - matrices[b][r][j])
        for r in range(n_users + 1)
        for j in range(n_users)
        for a, b in (("ort", "native"), ("ort", "torch"), ("native", "torch"))
    )
    with (OUT / "summary.txt").open("w") as f:
        f.write(f"E15/T11 continual on-device LoRA (5 users x {steps} steps, rank 8, lr {lr}, one exported graph)\n")
        f.write(f"users: rotations {ANGLES} deg, 1024 train / 2000 eval each; adapter NOT reset between users\n")
        f.write(f"scope: application-scenario demo; no anti-forgetting algorithm novelty claimed\n\n")
        for p in ("ort", "native", "torch"):
            f.write(f"[{p}] accuracy matrix (rows init/after_user_k x eval_user_j):\n")
            for label, row in zip(row_labels, matrices[p]):
                f.write(f"  {label:14s} " + "  ".join(f"{a:.4f}" for a in row) + "\n")
        f.write(f"\nthree-way consistency:\n")
        f.write(f"  worst |dacc| over all matrix entries: {worst_acc:.6f}\n")
        f.write(f"  worst per-user max|dloss|: ort/nat {max(r[1] for r in drift_rows):.2e}  "
                f"ort/tor {max(r[2] for r in drift_rows):.2e}  nat/tor {max(r[3] for r in drift_rows):.2e}\n")
        f.write(f"  worst per-user max|dW| (adapters): {max(r[4] for r in drift_rows):.2e}\n")
    print((OUT / "summary.txt").read_text())
    print(f"[done] wrote {OUT}/accuracy_matrix_*.csv, losses.csv, drift.csv, forgetting_curves.png, summary.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
