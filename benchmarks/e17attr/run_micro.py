# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T13 account 4: per-shape kernel boundary numbers, Camel nvm vs PyTorch eager.

For every GEMM shape of mlp_big / lenet_big's fc layers and every conv shape of
lenet_big, times one layer's forward and forward+backward kernels on both sides,
interleaved per trial, 4 threads, fresh processes:

  camel side: e17attr/micro.cml (with_grad step of a one-layer function)
  under std::macro std::nvm with CAMEL_TENSOR_OPSTATS=1; kernel us/call comes
  from the op-stats table (one call site per op, no shape aggregation).

  torch side: this file's worker (fresh python -c process per measurement),
  nn.Linear / nn.Conv2d with the same weights and input; fwd-only median and
  (fwd+bwd)-median over the same rep counts; loss = out.mean() as on the camel
  side.

Output rows per (shape, trial): camel us/call per kernel + camel step median,
torch fwd/bwd medians. The differencing used for the model-level op-stats is not
needed here: shapes are microsecond-scale and the compile step is excluded by
using only the timed reps' op counts (calls in the table / executed steps is
integral; the table's total is over 1+(W-1)+R steps, all of which run - micro.cml
uses sync arms).

Usage: .venv/Scripts/python e17attr/run_micro.py --out <dir> [--trials 5]
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parent.parent
E17 = BENCH_ROOT / "e17attr"
sys.path.insert(0, str(BENCH_ROOT))

from common.camel import _camel_env  # noqa: E402
from common.configs import camel_binary  # noqa: E402

# name, kind, (M, K, N) for lin; (N, Cin, Cout, K, H, W, pad) for conv; (numel,) for sgd
SHAPES = [
    ("mlp_fc1", "lin", (256, 784, 1024)),
    ("mlp_fc2", "lin", (256, 1024, 512)),
    ("mlp_fc3", "lin", (256, 512, 10)),
    ("mlp_sgd", "sgd", (784 * 1024 + 1024 + 1024 * 512 + 512 + 512 * 10 + 10,)),
    ("lenet_conv1", "conv_s1_p2", (128, 1, 16, 5, 28, 28, 2)),
    ("lenet_conv2", "conv_s1_p0", (128, 16, 48, 5, 14, 14, 0)),
    ("lenet_fc1", "lin", (128, 1200, 120)),
    ("lenet_fc2", "lin", (128, 120, 84)),
    ("lenet_fc3", "lin", (128, 84, 10)),
    ("lenet_sgd", "sgd", (16 * 25 + 16 + 48 * 16 * 25 + 48 + 1200 * 120 + 120 + 120 * 84 + 84 + 84 * 10 + 10,)),
]

OPROW = re.compile(r"^  (\S+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*$")
SETUP_OPS = {"load_npy", "set_num_threads"}
LIN_KERNELS = ["matmul", "matmul_grad_lhs", "matmul_grad_rhs"]
CONV_KERNELS = ["conv2d", "conv2d_input_grad", "conv2d_kernel_grad", "conv2d_bias_grad"]


def gen_inputs(shape, d: Path) -> None:
    rng = np.random.default_rng(1234)
    if shape[0].startswith("conv"):
        n, cin, cout, k, h, w, _pad = shape[1]
        np.save(d / "x.npy", rng.standard_normal((n, cin, h, w), dtype=np.float32))
        np.save(d / "w.npy", rng.standard_normal((cout, cin, k, k), dtype=np.float32))
        np.save(d / "b.npy", rng.standard_normal((cout,), dtype=np.float32))
    elif shape[0] == "sgd":
        (numel,) = shape[1]
        np.save(d / "x.npy", rng.standard_normal(numel, dtype=np.float32))
        np.save(d / "w.npy", rng.standard_normal(numel, dtype=np.float32))
    else:
        m, k, n = shape[1]
        np.save(d / "x.npy", rng.standard_normal((m, k), dtype=np.float32))
        np.save(d / "w.npy", rng.standard_normal((k, n), dtype=np.float32))


def camel_micro(shape, d: Path, warmup: int, reps: int, threads: int) -> dict:
    env = _camel_env("mlp_big", {})  # only used for CAMEL_HOME/CAMEL_PACKAGES plumbing
    env.pop("CAMEL_BENCH_WEIGHTS", None)
    env.pop("CAMEL_BENCH_INPUT", None)
    env.update(
        {
            "MICRO_OP": shape[0],
            "MICRO_DIR": str(d),
            "CAMEL_BENCH_WARMUP": str(warmup),
            "CAMEL_BENCH_REPS": str(reps),
            "CAMEL_BENCH_THREADS": str(threads),
            "CAMEL_TENSOR_OPSTATS": "1",
        }
    )
    cmd = [str(camel_binary()), str(E17 / "micro.cml"), "std::macro", "std::nvm"]
    proc = subprocess.run(cmd, cwd=E17, env=env, capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        raise RuntimeError(f"micro {shape[0]} failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
    lat = [float(m.group(1)) for m in re.finditer(r"CAMEL_BENCH latency_ms=([\d.eE+-]+)", proc.stdout)]
    steps = 1 + (warmup - 1) + reps
    ops = {}
    for line in proc.stderr.splitlines():
        m = OPROW.match(line)
        if m and m.group(1) not in SETUP_OPS:
            ops[m.group(1)] = {"calls_per_step": int(m.group(2)) / steps, "us_per_call": float(m.group(4))}
    if not lat or (shape[0] != "sgd" and not ops):
        raise RuntimeError(f"micro {shape[0]}: lost op-stats table or latencies (stderr {len(proc.stderr)} B)")
    return {"step_median_ms": statistics.median(lat), "ops": ops, "stderr": proc.stderr}


TORCH_WORKER = r"""
import json, sys, time
import numpy as np
import torch

d, kind, dims, warmup, reps, threads = sys.argv[1], sys.argv[2], json.loads(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
torch.set_num_threads(threads)
x = torch.from_numpy(np.load(d + "/x.npy")).requires_grad_(True)
w = torch.from_numpy(np.load(d + "/w.npy")).requires_grad_(True)
if kind.startswith("conv"):
    n, cin, cout, k, h, wd, pad = dims
    b = torch.from_numpy(np.load(d + "/b.npy")).requires_grad_(True)
    layer = torch.nn.Conv2d(cin, cout, k, stride=1, padding=pad, bias=True)
    with torch.no_grad():
        layer.weight.copy_(w); layer.bias.copy_(b)
    layer.weight.requires_grad_(True); layer.bias.requires_grad_(True)
    def fwd(): return layer(x)
else:
    m, k, n = dims
    layer = torch.nn.Linear(k, n, bias=False)
    with torch.no_grad():
        layer.weight.copy_(w.t())
    layer.weight.requires_grad_(True)
    def fwd(): return layer(x)

def step():
    for p in layer.parameters(): p.grad = None
    if x.grad is not None: x.grad = None
    loss = fwd().mean()
    loss.backward()

for _ in range(warmup): step()
ft, bt = [], []
for _ in range(reps):
    t0 = time.perf_counter(); y = fwd(); ft.append((time.perf_counter() - t0) * 1e3)
    t0 = time.perf_counter()
    for p in layer.parameters(): p.grad = None
    y.mean().backward()
    bt.append((time.perf_counter() - t0) * 1e3)
print(json.dumps({"fwd_ms": ft, "bwd_ms": bt}))
"""


def torch_micro(shape, d: Path, warmup: int, reps: int, threads: int) -> dict:
    proc = subprocess.run(
        [sys.executable, "-c", TORCH_WORKER, str(d), shape[0], json.dumps(shape[1]), str(warmup), str(reps), str(threads)],
        capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        raise RuntimeError(f"torch micro {shape[0]} failed:\n{proc.stderr[-2000:]}")
    res = json.loads(proc.stdout.strip().splitlines()[-1])
    return {"fwd_median_ms": statistics.median(res["fwd_ms"]), "bwd_median_ms": statistics.median(res["bwd_ms"])}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--trials", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=10)
    ap.add_argument("--reps", type=int, default=30)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--shapes", default="", help="comma filter on shape names")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    wanted = set(args.shapes.split(",")) if args.shapes else None
    rows = []
    for name, kind, dims in SHAPES:
        if wanted and name not in wanted:
            continue
        shape = (kind, dims)
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            gen_inputs(shape, d)
            for trial in range(1, args.trials + 1):
                cam = camel_micro(shape, d, args.warmup, args.reps, args.threads)
                if kind == "sgd":
                    # nn:sgd is invisible to op-stats: the whole step is the fused
                    # update (plus ~us of sync dispatch), so the step median is the
                    # kernel wall time. No torch equivalent here (see torch_step_split).
                    row = {
                        "shape": name,
                        "kind": kind,
                        "dims": json.dumps(dims),
                        "trial": trial,
                        "camel_fwd_us": round(cam["step_median_ms"] * 1e3, 2),
                        "camel_bwd_us": "",
                        "camel_ops_json": json.dumps(cam["ops"], sort_keys=True),
                        "torch_fwd_us": "",
                        "torch_bwd_us": "",
                    }
                    rows.append(row)
                    print(f"{name:12s} t{trial}  sgd step camel {row['camel_fwd_us']:9.2f} us", flush=True)
                    continue
                tor = torch_micro(shape, d, args.warmup, args.reps, args.threads)
                kernels = LIN_KERNELS if kind == "lin" else CONV_KERNELS
                cam_fwd = cam["ops"].get(kernels[0], {}).get("us_per_call", float("nan"))
                cam_bwd = sum(cam["ops"].get(k, {}).get("us_per_call", 0.0) for k in kernels[1:])
                row = {
                    "shape": name,
                    "kind": kind,
                    "dims": json.dumps(dims),
                    "trial": trial,
                    "camel_fwd_us": round(cam_fwd, 2),
                    "camel_bwd_us": round(cam_bwd, 2),
                    "camel_ops_json": json.dumps(cam["ops"], sort_keys=True),
                    "torch_fwd_us": round(tor["fwd_median_ms"] * 1e3, 2),
                    "torch_bwd_us": round(tor["bwd_median_ms"] * 1e3, 2),
                }
                rows.append(row)
                (args.out / f"raw_micro_{name}_t{trial}.stderr.txt").write_text(cam["stderr"])
                print(
                    f"{name:12s} t{trial}  fwd camel {cam_fwd:9.2f} us  torch {row['torch_fwd_us']:9.2f} us  "
                    f"(x{cam_fwd / max(row['torch_fwd_us'], 1e-9):5.2f})   bwd camel {cam_bwd:9.2f} us  torch {row['torch_bwd_us']:9.2f} us  "
                    f"(x{cam_bwd / max(row['torch_bwd_us'], 1e-9):5.2f})",
                    flush=True,
                )

    cols = ["shape", "kind", "dims", "trial", "camel_fwd_us", "camel_bwd_us", "torch_fwd_us", "torch_bwd_us", "camel_ops_json"]
    mode = "a" if args.shapes else "w"
    with open(args.out / "micro.csv", mode, newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        if mode == "w":
            w.writeheader()
        w.writerows(rows)
    print(f"[done] wrote {args.out / 'micro.csv'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
