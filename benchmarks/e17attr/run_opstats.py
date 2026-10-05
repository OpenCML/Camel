"""T13 account 2: per-operator kernel time vs step time (CAMEL_TENSOR_OPSTATS=1).

Two estimators, reported side by side:

  same-process (primary): kernel/step = (long run's op total - setup ops) /
  executed steps, against the long run's median timed latency. Kernel and step
  time come from one process, so the gap is real non-kernel time; the compile
  step inflates the kernel sum by ~(compile-steady)/steps (~1-3% at these rep
  counts), biasing non-kernel time DOWN (conservative for the overhead claim).

  differenced (cross-check): two runs with the same warmup and reps R0 < R1;
  (kernel_B - kernel_A)/(R1-R0) cancels the compile step exactly but adds
  process-to-process variance, which dominates for the fast opt config.

Non-kernel time covers nvm node scheduling, closure dispatch, allocation, and
ops the tensor registry does not wrap (nn:sgd - measured separately in
run_micro.py). Op call counts from differencing are exact (integral per step).

Usage:
  .venv/Scripts/python e17attr/run_opstats.py --out <dir> [--trials 3]
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import subprocess
import sys
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BENCH_ROOT))

from common.camel import _camel_env  # noqa: E402
from common.configs import camel_binary, camel_model  # noqa: E402
from train import CAMEL_CONFIGS  # noqa: E402

MODELS = ["mlp_big", "lenet_big"]
CONFIGS = ["camel_nvm", "camel_opt_nvm"]
SETUP_OPS = {"load_npy", "set_num_threads"}

OPROW = re.compile(r"^  (\S+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*$")


def run_once(model: str, config: str, warmup: int, reps: int, threads: int) -> dict:
    env = _camel_env(
        model,
        {
            "CAMEL_BENCH_WORKLOAD": "train",
            # This account is about the stateless protocol (updates not consumed), where
            # simplify removes the backward pass; train.py's default is now the stateful loop.
            "CAMEL_BENCH_TRAIN_PROTOCOL": "stateless",
            "CAMEL_BENCH_WARMUP": str(warmup),
            "CAMEL_BENCH_REPS": str(reps),
            "CAMEL_BENCH_THREADS": str(threads),
            "CAMEL_TENSOR_OPSTATS": "1",
        },
    )
    cmd = [str(camel_binary()), str(camel_model(model)), *CAMEL_CONFIGS[config]]
    proc = subprocess.run(cmd, cwd=camel_model(model).parent, env=env, capture_output=True, text=True, timeout=900)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")

    lat = [float(m.group(1)) for m in re.finditer(r"CAMEL_BENCH latency_ms=([\d.eE+-]+)", proc.stdout)]
    if len(lat) != reps:
        raise RuntimeError(f"expected {reps} latency lines, got {len(lat)}:\n{proc.stdout}")

    ops = {}
    for line in proc.stderr.splitlines():
        m = OPROW.match(line)
        if m:
            ops[m.group(1)] = {"calls": int(m.group(2)), "ms": float(m.group(3))}
    if not ops:
        raise RuntimeError(f"no op-stats table in stderr:\n{proc.stderr[-2000:]}")
    return {"lat": lat, "ops": ops, "stderr": proc.stderr}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--models", default=",".join(MODELS))
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--reps0", type=int, default=5, help="short-run reps")
    ap.add_argument("--reps1", type=int, default=25, help="long-run reps")
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for trial in range(1, args.trials + 1):
        for model in args.models.split(","):
            for config in args.configs.split(","):
                a = run_once(model, config, args.warmup, args.reps0, args.threads)
                b = run_once(model, config, args.warmup, args.reps1, args.threads)
                dr = args.reps1 - args.reps0
                (args.out / f"raw_{model}_{config}_t{trial}_short.stderr.txt").write_text(a["stderr"])
                (args.out / f"raw_{model}_{config}_t{trial}_long.stderr.txt").write_text(b["stderr"])

                names = sorted(set(a["ops"]) | set(b["ops"]))
                ops = {}
                for n in names:
                    ca, cb = a["ops"].get(n, {"calls": 0, "ms": 0.0}), b["ops"].get(n, {"calls": 0, "ms": 0.0})
                    ops[n] = {
                        "calls_per_step": (cb["calls"] - ca["calls"]) / dr,
                        "ms_per_step": (cb["ms"] - ca["ms"]) / dr,
                    }
                kernel = sum(v["ms_per_step"] for k, v in ops.items() if k not in SETUP_OPS)
                step = statistics.median(b["lat"])
                # Same-process estimator (primary): kernel and latency from the long
                # run only. The first (compile) step inflates the kernel sum by
                # roughly (compile-steady)/(1+(W-1)+R1) ~= 1-3% here, so this slightly
                # UNDERSTATES non-kernel time - conservative for the overhead claim.
                steps_b = 1 + (args.warmup - 1) + args.reps1
                kernel_sp = sum(v["ms"] for k, v in b["ops"].items() if k not in SETUP_OPS) / steps_b
                ops_sp = {n: {"calls_per_step": v["calls"] / steps_b, "ms_per_step": v["ms"] / steps_b}
                          for n, v in b["ops"].items() if n not in SETUP_OPS}
                rows.append(
                    {
                        "model": model,
                        "config": config,
                        "trial": trial,
                        "threads": args.threads,
                        "warmup": args.warmup,
                        "reps": f"{args.reps0}+{args.reps1}",
                        "kernel_per_step_ms": round(kernel_sp, 4),
                        "step_median_ms": round(step, 4),
                        "nonkernel_ms": round(step - kernel_sp, 4),
                        "nonkernel_pct": round(100.0 * (step - kernel_sp) / step, 2),
                        "kernel_diff_ms": round(kernel, 4),
                        "nonkernel_diff_ms": round(step - kernel, 4),
                        "ops_json": json.dumps(ops_sp, sort_keys=True),
                    }
                )
                print(
                    f"t{trial} {model:10s} {config:14s} step {step:8.3f} ms  kernel {kernel_sp:8.3f} ms/step  "
                    f"non-kernel {step - kernel_sp:7.3f} ms ({100.0 * (step - kernel_sp) / step:5.1f}%)  "
                    f"[diff xcheck: kernel {kernel:7.3f}]",
                    flush=True,
                )

    cols = ["model", "config", "trial", "threads", "warmup", "reps",
            "kernel_per_step_ms", "step_median_ms", "nonkernel_ms", "nonkernel_pct",
            "kernel_diff_ms", "nonkernel_diff_ms", "ops_json"]
    with open(args.out / "opstats.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    print(f"[done] wrote {args.out / 'opstats.csv'} (+ raw stderr per trial)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
