"""Training workload: gradient agreement with PyTorch and training-step timing.

For each model, the loss is ``mean(y * y)`` of the benchmark model's output ``y``
on the benchmark input (the same in every framework), and a training step is the
loss, its gradient with respect to every weight, and an SGD update (lr 0.01).

Gradient check (``--check``): Camel's ``grad`` of the loss (dumped by the model's
training workload through ``nn.save_params``) is compared with PyTorch autograd
weight by weight. Weights are matched to torch parameters by value (directly or
transposed, as ``nn.Linear`` stores ``[out, in]``), so no per-model name mapping
is needed.

Timing (``--time``): PyTorch eager versus the Camel configurations below, each
run in ``--trials`` fresh processes, interleaved; the report gives the median
step time and a bootstrap 95% CI over trial medians.

Usage:
  .venv/bin/python train.py --check
  .venv/bin/python train.py --time --trials 5 --out results/train.json
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_ROOT))

from common import stats  # noqa: E402
from common.camel import _camel_env, parse_bench_lines  # noqa: E402
from common.configs import camel_binary, camel_model  # noqa: E402
from common.weights import load  # noqa: E402

MODELS = ["mlp", "gru", "transformer"]
LR = 0.01

# Camel configurations of the training workload: passes after `camel <model.cml>`.
CAMEL_CONFIGS: Dict[str, Tuple[str, ...]] = {
    "camel_nvm": ("std::macro", "std::nvm"),
    "camel_fvm": ("std::macro", "std::fvm"),
    "camel_fuse_nvm": ("std::macro", "tensor::fuse", "std::nvm"),
}


# ---------------------------------------------------------------- camel


def run_camel(model: str, passes: Tuple[str, ...], env: Dict[str, str], timeout: float = 600.0) -> str:
    child_env = _camel_env(model, {"CAMEL_BENCH_WORKLOAD": "train", **env})
    child_env.setdefault("CAMEL_BENCH_THREADS", str(os.cpu_count() or 1))
    child_env.setdefault("CAMEL_BENCH_WARMUP", "2")
    child_env.setdefault("CAMEL_BENCH_REPS", "10")
    cmd = [str(camel_binary()), str(camel_model(model)), *passes]
    proc = subprocess.run(
        cmd, cwd=camel_model(model).parent, env=child_env, capture_output=True, text=True, timeout=timeout
    )
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


def camel_gradients(model: str) -> Dict[str, np.ndarray]:
    with tempfile.TemporaryDirectory() as tmp:
        run_camel(model, CAMEL_CONFIGS["camel_nvm"], {"CAMEL_BENCH_GRADS": tmp})
        return {p.stem: np.load(p) for p in Path(tmp).glob("*.npy")}


# ---------------------------------------------------------------- torch


def torch_model(model: str, weights: Dict[str, np.ndarray]):
    import importlib

    module = importlib.import_module(f"{model}.model_torch")
    net = module.build_model(weights)
    net.train()
    for p in net.parameters():  # inference references may freeze some weights
        p.requires_grad_(True)
    return net


def torch_loss(net, x):
    y = net(x)
    return (y * y).mean()


def torch_gradients(model: str) -> Tuple[Dict[str, np.ndarray], Dict[str, np.ndarray]]:
    """(weight name -> gradient, weight name -> torch parameter name) by value matching."""
    import torch

    weights, x = load(model)
    net = torch_model(model, weights)
    loss = torch_loss(net, torch.from_numpy(x))
    loss.backward()
    grads, names = {}, {}
    params = [(n, p.detach().numpy(), p.grad.numpy()) for n, p in net.named_parameters() if p.grad is not None]
    for wname, w in weights.items():
        for pname, value, grad in params:
            if value.shape == w.shape and np.array_equal(value, w):
                grads[wname], names[wname] = grad, pname
                break
            if value.ndim == 2 and value.T.shape == w.shape and np.array_equal(value.T, w):
                grads[wname], names[wname] = grad.T, pname + ".T"
                break
    return grads, names


def check(models: List[str], rtol: float) -> bool:
    ok = True
    for model in models:
        camel = camel_gradients(model)
        torch_grads, names = torch_gradients(model)
        print(f"== {model}")
        # Scale errors by the weight's largest gradient, floored so that weights whose exact
        # gradient is zero (e.g. the key bias under softmax, which shifts every score of a
        # query equally) compare rounding noise against the model's gradient scale instead.
        floor = 1e-3 * max(float(np.max(np.abs(g))) for g in torch_grads.values())
        for name in sorted(torch_grads):
            ref = torch_grads[name]
            got = camel.get(name)
            if got is None:
                print(f"  {name:12s} MISSING in Camel's gradient")
                ok = False
                continue
            scale = max(float(np.max(np.abs(ref))), floor) or 1.0
            err = float(np.max(np.abs(got - ref))) / scale
            status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
            ok &= status == "ok"
            print(f"  {name:12s} {status:8s} max|diff|/max|ref| = {err:.2e}  ({names[name]}, {list(ref.shape)})")
        unmatched = set(camel) - set(torch_grads)
        if unmatched:
            print(f"  (no torch parameter holds: {sorted(unmatched)})")
    return ok


# ---------------------------------------------------------------- timing


def torch_step_times(model: str, reps: int, warmup: int, threads: int) -> List[float]:
    import torch

    torch.set_num_threads(threads)
    weights, x = load(model)
    net = torch_model(model, weights)
    opt = torch.optim.SGD(net.parameters(), lr=LR)
    xt = torch.from_numpy(x)

    def step():
        opt.zero_grad(set_to_none=True)
        torch_loss(net, xt).backward()
        opt.step()

    for _ in range(warmup):
        step()
    times = []
    for _ in range(reps):
        start = time.perf_counter()
        step()
        times.append((time.perf_counter() - start) * 1000.0)
    return times


def torch_trial(model: str, reps: int, warmup: int, threads: int) -> List[float]:
    """One trial in a fresh process, like the Camel trials."""
    code = (
        "import json, sys; sys.path.insert(0, %r); import train; "
        "print(json.dumps(train.torch_step_times(%r, %d, %d, %d)))" % (str(BENCH_ROOT), model, reps, warmup, threads)
    )
    out = subprocess.run(
        [sys.executable, "-c", code], cwd=BENCH_ROOT, capture_output=True, text=True, check=True
    ).stdout
    return json.loads(out.strip().splitlines()[-1])


def camel_trial(model: str, passes: Tuple[str, ...], reps: int, warmup: int, threads: int) -> List[float]:
    out = run_camel(
        model,
        passes,
        {"CAMEL_BENCH_REPS": str(reps), "CAMEL_BENCH_WARMUP": str(warmup), "CAMEL_BENCH_THREADS": str(threads)},
    )
    return parse_bench_lines(out).latencies_ms


def time_models(models: List[str], trials: int, reps: int, warmup: int, threads: int) -> dict:
    configs = ["torch_eager", *CAMEL_CONFIGS]
    samples: Dict[str, Dict[str, List[float]]] = {m: {c: [] for c in configs} for m in models}
    for trial in range(trials):
        for model in models:
            # Rotate the order per trial so no configuration always runs first.
            order = configs[trial % len(configs) :] + configs[: trial % len(configs)]
            for config in order:
                if config == "torch_eager":
                    times = torch_trial(model, reps, warmup, threads)
                else:
                    times = camel_trial(model, CAMEL_CONFIGS[config], reps, warmup, threads)
                samples[model][config].append(stats.median(times))
                print(f"trial {trial + 1}/{trials} {model:12s} {config:16s} {stats.median(times):8.3f} ms")
    report = {"trials": trials, "reps": reps, "threads": threads, "models": {}}
    for model in models:
        rows = {}
        base = stats.median(samples[model]["torch_eager"])
        for config in configs:
            meds = samples[model][config]
            lo, hi = stats.bootstrap_ci(meds)
            rows[config] = {
                "median_ms": stats.median(meds),
                "ci95_ms": [lo, hi],
                "trial_medians_ms": meds,
                "speedup_vs_torch": base / stats.median(meds),
            }
        report["models"][model] = rows
    return report


def print_report(report: dict) -> None:
    print()
    print(f"training step (median of {report['trials']} trials x {report['reps']} steps, {report['threads']} threads)")
    for model, rows in report["models"].items():
        print(f"== {model}")
        for config, row in rows.items():
            lo, hi = row["ci95_ms"]
            print(
                f"  {config:16s} {row['median_ms']:8.3f} ms  [{lo:.3f}, {hi:.3f}]  "
                f"x{row['speedup_vs_torch']:.2f} vs torch eager"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default=",".join(MODELS))
    parser.add_argument("--check", action="store_true", help="compare gradients with PyTorch")
    parser.add_argument("--rtol", type=float, default=1e-4, help="gradient tolerance relative to max |grad| (see check)")
    parser.add_argument("--time", action="store_true", help="time training steps")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--threads", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    models = [m for m in args.models.split(",") if m]
    ok = True
    if args.check or not args.time:
        ok = check(models, args.rtol)
        print("gradient check:", "PASS" if ok else "FAIL")
    if args.time:
        report = time_models(models, args.trials, args.reps, args.warmup, args.threads)
        print_report(report)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(json.dumps(report, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
