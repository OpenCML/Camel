# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E3 context only (§A.4): PyTorch eager GPU training-step latency for the same
workload (loss mean(y*y), one SGD step lr 0.01, same weights and input).

These numbers are *context* for the GPU portability table — the GPU compute in
E3's CUDA leg is ONNX Runtime's kernels, so this is not a Camel-vs-PyTorch GPU
kernel race and must never be framed as one.

Usage (server, venv-bench):
  CUDA_VISIBLE_DEVICES=1 python e3_torch_gpu_context.py --models mlp,gru,transformer \
      --reps 50 --warmup 10 --out e3/bundle/e3_torch_gpu_context.csv
"""

from __future__ import annotations

import argparse
import csv
import sys
import time
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))


def time_model(model: str, reps: int, warmup: int, lr: float) -> dict:
    import torch

    import train
    from common.weights import load

    weights, x = load(model)
    device = "cuda"
    net = train.torch_model(model, weights).to(device)
    opt = torch.optim.SGD(net.parameters(), lr=lr)
    xt = torch.from_numpy(x).to(device)

    def step():
        opt.zero_grad(set_to_none=True)
        train.torch_loss(net, xt).backward()
        opt.step()

    for _ in range(warmup):
        step()
    torch.cuda.synchronize()
    times = []
    for _ in range(reps):
        begin = time.perf_counter()
        step()
        torch.cuda.synchronize()
        times.append((time.perf_counter() - begin) * 1000.0)
    times.sort()
    return {
        "model": model,
        "median_ms": f"{times[len(times) // 2]:.3f}",
        "p10_ms": f"{times[len(times) // 10]:.3f}",
        "p90_ms": f"{times[(len(times) * 9) // 10]:.3f}",
        "reps": reps,
        "note": "context only; ORT CUDA EP runs ORT kernels, not a Camel GPU claim",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--reps", type=int, default=50)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--lr", type=float, default=0.01)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    import torch

    print(f"torch {torch.__version__}, cuda device: {torch.cuda.get_device_name(0)}")
    rows = [time_model(m, args.reps, args.warmup, args.lr) for m in args.models.split(",") if m]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["model", "median_ms", "p10_ms", "p90_ms", "reps", "note"])
        w.writeheader()
        w.writerows(rows)
    for r in rows:
        print(f"  {r['model']:12s} {r['median_ms']} ms/step (p10 {r['p10_ms']}, p90 {r['p90_ms']})")
    print(f"[done] wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
