"""E8 cold-start comparison (MLSys 2027 eval): wall time from "source + fresh
process" to the FIRST completed training step, per (model, config):

  camel_nvm / camel_opt_nvm  the `camel` binary itself (macro [+ simplify] + nvm):
                             process wall is the whole story
  torch_eager / torch_compile  python child: import time + build + first step
                               (the runner's compile_s covers build+first step)
  jax_jit                    python child: import jax + jit trace + XLA compile + first step
  ort_training               python child (venv-orttrain): import + generate_artifacts
                               + session + first step

Every measurement is a fresh subprocess (no warm caches between trials), 10
interleaved trials with per-trial shuffled order and bootstrap 95% CI (paired
by trial) — same discipline as E2. Outputs e8_cold_start.csv (+ _summary.csv)
and a log-scale bar chart e8_cold_start.png.

Server usage:
  taskset -c 0-3 venv-bench/bin/python e8_cold_start.py \
      --python-ort ~/camel-eval/venv-orttrain/bin/python --out ~/camel-eval/e8/e8.csv
"""

from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
import time
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common import stats  # noqa: E402
import train  # noqa: E402

ROW = "E8ROW "
CAMEL_CONFIGS = {"camel_nvm": ("std::macro", "std::nvm"), "camel_opt_nvm": ("std::macro", "std::opt::simplify", "std::nvm")}
PY_CONFIGS = {
    "torch_eager": ("train", "torch_trial_row({model!r}, 1, 1, {threads})", "torch"),
    "torch_compile": ("baselines.torch_compile_runner", "trial_row({model!r}, 'default', 1, 1, {threads})", "torch"),
    "jax_jit": ("baselines.jax_runner", "trial_row({model!r}, 1, 1, {threads})", "jax"),
    "ort_training": ("baselines.ort_training_runner", "trial_row({model!r}, 1, 1, {threads})", "ort"),
}
IMPORTS = {"torch": "import torch", "jax": "import jax, jaxlib", "ort": "import onnxruntime, onnx"}


def camel_child(model: str, passes: tuple, threads: int, timeout: float) -> dict:
    """Whole-process wall for the camel binary to finish one training step."""
    from common.camel import _camel_env, parse_bench_lines
    from common.configs import camel_binary, camel_model

    env = _camel_env(
        model,
        {"CAMEL_BENCH_WORKLOAD": "train", "CAMEL_BENCH_WARMUP": "0", "CAMEL_BENCH_REPS": "1", "CAMEL_BENCH_THREADS": str(threads)},
    )
    cml = camel_model(model)
    t0 = time.perf_counter()
    proc = subprocess.run([str(camel_binary()), str(cml), *passes], cwd=cml.parent, env=env, capture_output=True, text=True, timeout=timeout)
    wall = time.perf_counter() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"camel exited {proc.returncode}: {proc.stderr.strip()[-300:]}")
    res = parse_bench_lines(proc.stdout)
    return {"wall_s": wall, "compile_s": res.summary.get("compile_ms", 0.0) / 1000.0, "import_s": 0.0}


def python_child(python: str, module: str, call: str, import_stmt: str, timeout: float) -> dict:
    code = (
        f"import json, sys, time; sys.path.insert(0, {str(BENCH_ROOT)!r}); "
        f"_t0 = time.perf_counter(); {import_stmt}; _t1 = time.perf_counter(); "
        f"import {module} as _m; _r = _m.{call}; "
        f"print({ROW!r} + json.dumps({{'import_s': _t1 - _t0, 'compile_s': _r.get('compile_s'), 'warmup_s': _r.get('warmup_s')}}))"
    )
    t0 = time.perf_counter()
    proc = subprocess.run([python, "-c", code], cwd=BENCH_ROOT, capture_output=True, text=True, timeout=timeout)
    wall = time.perf_counter() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"child exited {proc.returncode}: {proc.stderr.strip()[-300:]}")
    for line in reversed(proc.stdout.splitlines()):
        if line.startswith(ROW):
            return {"wall_s": wall, **json.loads(line[len(ROW) :])}
    raise RuntimeError("no E8ROW line in child output")


def measure(model: str, config: str, threads: int, pythons: dict, timeout: float) -> dict:
    if config in CAMEL_CONFIGS:
        return camel_child(model, CAMEL_CONFIGS[config], threads, timeout)
    module, template, key = PY_CONFIGS[config]
    return python_child(pythons[key], module, template.format(model=model, threads=threads), IMPORTS[key], timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--configs", default="camel_nvm,camel_opt_nvm,torch_eager,torch_compile,jax_jit,ort_training")
    parser.add_argument("--trials", type=int, default=10)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=900.0)
    parser.add_argument("--python-torch", default=sys.executable)
    parser.add_argument("--python-jax", default=sys.executable)
    parser.add_argument("--python-ort", default=sys.executable)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    models = [m for m in args.models.split(",") if m]
    configs = [c for c in args.configs.split(",") if c]
    pythons = {"torch": args.python_torch, "jax": args.python_jax, "ort": args.python_ort}
    rng = random.Random(args.seed)
    rows = []
    for trial in range(1, args.trials + 1):
        for model in models:
            order = configs[:]
            rng.shuffle(order)
            for config in order:
                try:
                    r = measure(model, config, args.threads, pythons, args.timeout)
                    rows.append({"model": model, "config": config, "trial": trial, "wall_s": r["wall_s"], "import_s": r.get("import_s") or 0.0, "compile_s": r.get("compile_s") or 0.0, "status": "ok"})
                    print(f"t{trial} {model:12s} {config:16s} wall {r['wall_s']:7.3f}s (import {r.get('import_s') or 0:5.2f}s, compile {r.get('compile_s') or 0:6.3f}s)", flush=True)
                except Exception as exc:
                    rows.append({"model": model, "config": config, "trial": trial, "wall_s": None, "import_s": None, "compile_s": None, "status": f"failed: {str(exc)[:200]}"})
                    print(f"t{trial} {model:12s} {config:16s} FAILED (archived): {str(exc)[:160]}", flush=True)

    import csv

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["model", "config", "trial", "wall_s", "import_s", "compile_s", "status"])
        w.writeheader()
        w.writerows(rows)

    # summary: median wall per (model, config) + paired bootstrap ratio vs camel_nvm
    summary = []
    for model in models:
        base = [r["wall_s"] for r in rows if r["model"] == model and r["config"] == "camel_nvm" and r["status"] == "ok"]
        for config in configs:
            cur = [r["wall_s"] for r in rows if r["model"] == model and r["config"] == config and r["status"] == "ok"]
            if not cur:
                summary.append({"model": model, "config": config, "status": "failed", "median_wall_s": "", "n": 0, "x_vs_camel_nvm": "", "ci95": ""})
                continue
            med = sorted(cur)[len(cur) // 2]
            ratio, lo, hi = ("", "", "")
            if config != "camel_nvm" and len(base) == len(cur) and base:
                ratio, lo, hi = stats.ratio_ci(cur, base, paired=True, seed=args.seed)
            status = "ok" if len(cur) == args.trials else f"ok({len(cur)}/{args.trials})"
            summary.append(
                {
                    "model": model,
                    "config": config,
                    "status": status,
                    "median_wall_s": f"{med:.4f}",
                    "n": len(cur),
                    "x_vs_camel_nvm": f"{ratio:.2f}" if ratio != "" else "1.00",
                    "ci95": f"[{lo:.2f}, {hi:.2f}]" if lo != "" else "",
                }
            )
    spath = args.out.with_name(args.out.stem + "_summary.csv")
    with spath.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["model", "config", "status", "median_wall_s", "n", "x_vs_camel_nvm", "ci95"])
        w.writeheader()
        w.writerows(summary)
    for s in summary:
        print(f"  {s['model']:12s} {s['config']:16s} wall {s['median_wall_s']}s  x{s['x_vs_camel_nvm']} {s['ci95']}")
    print(f"[done] wrote {args.out} and {spath}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
