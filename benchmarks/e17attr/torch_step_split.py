# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T13 account (torch side): wall-clock segmentation of the PyTorch eager
training step into forward, backward, and SGD-update phases.

A profiler's self_cpu_time_total is CPU time summed over threads and exceeds
wall time on multi-threaded ops, so instead each step is split by perf_counter
into its three contiguous phases (they tile the step; their sum reproduces the
step wall by construction, verified against independently timed full steps).
This gives exactly the quantity the attribution needs: the fraction of the
eager step spent on the work that camel_opt_nvm's DCE eliminates
(backward + update) versus the work it keeps (forward + loss).

Per (model, trial), in a fresh process, built exactly like train.py's
torch_trial_row (same weights, loss mean(y^2), SGD lr 0.01).

Usage: .venv/Scripts/python e17attr/torch_step_split.py --out <dir> [--trials 3]
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent.parent

WORKER = r"""
import json, statistics, sys, time
sys.path.insert(0, sys.argv[1])
import torch
from common.weights import load
import train as T

model, reps, warmup, threads = sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
torch.set_num_threads(threads)
weights, x = load(model)
net = T.torch_model(model, weights)
opt = torch.optim.SGD(net.parameters(), lr=T.LR)
xt = torch.from_numpy(x)

def step():
    opt.zero_grad(set_to_none=True)
    T.torch_loss(net, xt).backward()
    opt.step()

for _ in range(warmup):
    step()

full, fwd, bwd, sgd = [], [], [], []
for _ in range(reps):
    t0 = time.perf_counter()
    opt.zero_grad(set_to_none=True)
    loss = T.torch_loss(net, xt)
    t1 = time.perf_counter()
    loss.backward()
    t2 = time.perf_counter()
    opt.step()
    t3 = time.perf_counter()
    full.append((t3 - t0) * 1e3)
    fwd.append((t1 - t0) * 1e3)
    bwd.append((t2 - t1) * 1e3)
    sgd.append((t3 - t2) * 1e3)

# independent full-step timing (no intra-step counters) as the cross-check
plain = []
for _ in range(reps):
    t0 = time.perf_counter()
    step()
    plain.append((time.perf_counter() - t0) * 1e3)

print("RESULT " + json.dumps({
    "full_ms": statistics.median(full), "fwd_ms": statistics.median(fwd),
    "bwd_ms": statistics.median(bwd), "sgd_ms": statistics.median(sgd),
    "plain_ms": statistics.median(plain),
}))
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--models", default="mlp_big,lenet_big")
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=40)
    ap.add_argument("--reps", type=int, default=20)
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    rows = []
    for trial in range(1, args.trials + 1):
        for model in args.models.split(","):
            proc = subprocess.run(
                [sys.executable, "-c", WORKER, str(BENCH_ROOT), model, str(args.reps), str(args.warmup), str(args.threads)],
                capture_output=True, text=True, timeout=900)
            if proc.returncode != 0:
                raise RuntimeError(f"torch split {model} failed:\n{proc.stderr[-2000:]}")
            line = [l for l in proc.stdout.splitlines() if l.startswith("RESULT ")][-1]
            res = json.loads(line[len("RESULT "):])
            seg = res["fwd_ms"] + res["bwd_ms"] + res["sgd_ms"]
            rows.append(
                {
                    "model": model,
                    "trial": trial,
                    "threads": args.threads,
                    "full_ms": round(res["full_ms"], 4),
                    "fwd_ms": round(res["fwd_ms"], 4),
                    "bwd_ms": round(res["bwd_ms"], 4),
                    "sgd_ms": round(res["sgd_ms"], 4),
                    "plain_ms": round(res["plain_ms"], 4),
                    "seg_vs_plain_pct": round(100.0 * (seg - res["plain_ms"]) / res["plain_ms"], 2),
                    "bwd_plus_sgd_pct": round(100.0 * (res["bwd_ms"] + res["sgd_ms"]) / seg, 2),
                }
            )
            print(
                f"t{trial} {model:10s} full {res['full_ms']:7.3f} = fwd {res['fwd_ms']:6.3f} + bwd {res['bwd_ms']:6.3f} "
                f"+ sgd {res['sgd_ms']:5.3f}   (vs plain {res['plain_ms']:7.3f}, bwd+sgd {rows[-1]['bwd_plus_sgd_pct']:4.1f}%)",
                flush=True,
            )

    cols = ["model", "trial", "threads", "full_ms", "fwd_ms", "bwd_ms", "sgd_ms", "plain_ms", "seg_vs_plain_pct", "bwd_plus_sgd_pct"]
    with open(args.out / "torch_step_split.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    print(f"[done] wrote {args.out / 'torch_step_split.csv'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
