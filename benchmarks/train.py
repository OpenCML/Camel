"""Training workload: gradient agreement with PyTorch and training-step timing.

For each model, the loss is ``mean(y * y)`` of the benchmark model's output ``y``
on the benchmark input (the same in every framework), and a training step is the
loss, its gradient with respect to every weight, and an SGD update (lr 0.01).

Gradient check (``--check``): Camel's ``grad`` of the loss (dumped by the model's
training workload through ``nn.save_params``) is compared with PyTorch autograd
weight by weight. Weights are matched to torch parameters by value (directly or
transposed, as ``nn.Linear`` stores ``[out, in]``), so no per-model name mapping
is needed.

Training-step export (``--check-onnx``): the model's training step (loss,
gradient, SGD update) is exported with ``onnx.export_model`` as a function of
the parameters and the input, checked with ``onnx.checker``, and run once on
ONNX Runtime; its loss and updated parameters are compared with one step of
Camel's own execution (the loss it reports and ``w - lr * grad`` from the
dumped gradients).

Timing (``--time``): PyTorch eager versus the Camel configurations below, each
run in ``--trials`` fresh processes, interleaved. Output has run.py's format:
raw rows per trial (first step, warmup and steady-state step latencies, peak
RSS of the process running the model) and a summary with the median step time,
a bootstrap 95% CI over trial medians and the speedup over PyTorch, plus a
``.meta.json`` with the environment (common/envinfo.py).

Usage:
  .venv/bin/python train.py --check
  .venv/bin/python train.py --check-onnx
  .venv/bin/python train.py --time --trials 5 --out results/train.csv
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

from common import envinfo, stats  # noqa: E402
from common.camel import _camel_env, parse_bench_lines  # noqa: E402
from common.configs import camel_binary, camel_model  # noqa: E402
from run import _write_csv  # noqa: E402
from common.weights import load  # noqa: E402

MODELS = ["mlp", "gru", "transformer"]
LR = 0.01

# Camel configurations of the training workload: passes after `camel <model.cml>`.
CAMEL_CONFIGS: Dict[str, Tuple[str, ...]] = {
    "camel_nvm": ("std::macro", "std::nvm"),
    "camel_fvm": ("std::macro", "std::fvm"),
    "camel_fuse_nvm": ("std::macro", "tensor::fuse", "std::nvm"),
    # Generic simplification first: pullback closures collapse into straight-line code.
    "camel_opt_nvm": ("std::macro", "std::opt::simplify", "std::nvm"),
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


def camel_gradients(model: str, config: str = "camel_nvm") -> Dict[str, np.ndarray]:
    with tempfile.TemporaryDirectory() as tmp:
        run_camel(model, CAMEL_CONFIGS[config], {"CAMEL_BENCH_GRADS": tmp})
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


def check(models: List[str], rtol: float, configs: List[str]) -> bool:
    ok = True
    for model, config in [(m, c) for m in models for c in configs]:
        camel = camel_gradients(model, config)
        torch_grads, names = torch_gradients(model)
        print(f"== {model} ({config})")
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


# ---------------------------------------------------------------- onnx export


def camel_loss(model: str) -> float:
    """The loss of one native training step (every timed step starts from the same weights)."""
    import re

    out = run_camel(model, CAMEL_CONFIGS["camel_nvm"], {"CAMEL_BENCH_WARMUP": "1", "CAMEL_BENCH_REPS": "1"})
    found = re.search(r"loss=([-+0-9.eE]+)", out)
    if not found:
        raise RuntimeError(f"no loss in Camel's output:\n{out}")
    return float(found.group(1))


def check_onnx(models: List[str], rtol: float) -> bool:
    import onnx
    import onnxruntime as ort

    ok = True
    for model in models:
        print(f"== {model} (training step exported to ONNX)")
        weights, x = load(model)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / f"{model}_step.onnx"
            start = time.perf_counter()
            run_camel(model, (), {"CAMEL_BENCH_WORKLOAD": "train_export", "CAMEL_BENCH_EXPORT": str(path)})
            export_s = time.perf_counter() - start
            proto = onnx.load(str(path))
            onnx.checker.check_model(proto, full_check=True)
            session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
            feeds = {i.name: (x if i.name == "input1" else weights[i.name]) for i in session.get_inputs()}
            outputs = dict(zip([o.name for o in session.get_outputs()], session.run(None, feeds)))
        print(f"  exported in {export_s:.2f} s: {len(proto.graph.node)} nodes, onnx.checker ok")
        ref_loss = camel_loss(model)
        got_loss = float(outputs["output_0"])
        loss_err = abs(got_loss - ref_loss) / max(abs(ref_loss), 1e-12)
        status = "ok" if loss_err <= rtol else "MISMATCH"
        ok &= status == "ok"
        print(f"  {'loss':12s} {status:8s} |diff|/|ref| = {loss_err:.2e}  (ORT {got_loss:.6g}, Camel {ref_loss:.6g})")
        grads = camel_gradients(model)
        for name in sorted(weights):
            got = outputs.get(f"output_1_{name}")
            if got is None:
                print(f"  {name:12s} MISSING in the exported step's outputs")
                ok = False
                continue
            ref = weights[name] - LR * grads[name]
            scale = float(np.max(np.abs(weights[name]))) or 1.0
            err = float(np.max(np.abs(got - ref))) / scale
            status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
            ok &= status == "ok"
            print(f"  {name:12s} {status:8s} max|diff|/max|w| = {err:.2e}")
    return ok


# ---------------------------------------------------------------- timing


COLUMNS = [
    "model",
    "config",
    "trial",
    "status",
    "threads",
    "warmup",
    "reps",
    "compile_s",
    "warmup_s",
    "latency_median_ms",
    "latency_p10_ms",
    "latency_p90_ms",
    "latency_mean_ms",
    "peak_rss_mb",
    "notes",
]


def _latency_fields(times: List[float]) -> Dict[str, float]:
    return {
        "latency_median_ms": stats.median(times),
        "latency_p10_ms": stats.percentile(times, 10),
        "latency_p90_ms": stats.percentile(times, 90),
        "latency_mean_ms": sum(times) / len(times),
    }


def torch_trial_row(model: str, reps: int, warmup: int, threads: int) -> dict:
    """One PyTorch trial, run inside a fresh process (see torch_trial)."""
    import torch

    from common.memory import peak_rss_self_kb

    torch.set_num_threads(threads)
    weights, x = load(model)
    net = torch_model(model, weights)
    opt = torch.optim.SGD(net.parameters(), lr=LR)
    xt = torch.from_numpy(x)

    def step():
        opt.zero_grad(set_to_none=True)
        torch_loss(net, xt).backward()
        opt.step()

    start = time.perf_counter()
    step()
    compile_s = time.perf_counter() - start
    for _ in range(warmup - 1):
        step()
    warmup_s = time.perf_counter() - start
    times = []
    for _ in range(reps):
        begin = time.perf_counter()
        step()
        times.append((time.perf_counter() - begin) * 1000.0)
    return {
        "compile_s": compile_s,
        "warmup_s": warmup_s,
        **_latency_fields(times),
        "peak_rss_mb": peak_rss_self_kb() / 1024.0,
    }


def camel_trial_row(model: str, passes: Tuple[str, ...], reps: int, warmup: int, threads: int) -> dict:
    """One Camel trial; runs inside a fresh Python process that has no other child (see
    camel_trial), so the kernel's children high-water mark is this run's peak RSS."""
    from common.memory import run_with_peak_rss

    env = _camel_env(
        model,
        {
            "CAMEL_BENCH_WORKLOAD": "train",
            "CAMEL_BENCH_REPS": str(reps),
            "CAMEL_BENCH_WARMUP": str(warmup),
            "CAMEL_BENCH_THREADS": str(threads),
        },
    )
    cmd = [str(camel_binary()), str(camel_model(model)), *passes]
    code, stdout, stderr, peak_kb = run_with_peak_rss(cmd, camel_model(model).parent, env)
    if code != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({code}): {stderr.strip()[-400:]}")
    res = parse_bench_lines(stdout)
    return {
        "compile_s": res.summary.get("compile_ms", float("nan")) / 1000.0,
        "warmup_s": res.summary.get("warmup_ms", float("nan")) / 1000.0,
        **_latency_fields(res.latencies_ms),
        "peak_rss_mb": peak_kb / 1024.0 if peak_kb else None,
    }


def _in_fresh_process(call: str) -> dict:
    code = f"import json, sys; sys.path.insert(0, {str(BENCH_ROOT)!r}); import train; print(json.dumps(train.{call}))"
    proc = subprocess.run([sys.executable, "-c", code], cwd=BENCH_ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip()[-400:])
    return json.loads(proc.stdout.strip().splitlines()[-1])


def time_models(models: List[str], trials: int, reps: int, warmup: int, threads: int) -> List[dict]:
    """Raw rows, one per (model, config, trial), in run.py's format."""
    configs = ["torch_eager", *CAMEL_CONFIGS]
    rows: List[dict] = []
    for trial in range(1, trials + 1):
        for model in models:
            # Rotate the order per trial so no configuration always runs first.
            k = (trial - 1) % len(configs)
            for config in configs[k:] + configs[:k]:
                base = {"model": model, "config": config, "trial": trial, "threads": threads, "warmup": warmup, "reps": reps}
                try:
                    if config == "torch_eager":
                        res = _in_fresh_process(f"torch_trial_row({model!r}, {reps}, {warmup}, {threads})")
                    else:
                        passes = CAMEL_CONFIGS[config]
                        res = _in_fresh_process(f"camel_trial_row({model!r}, {passes!r}, {reps}, {warmup}, {threads})")
                    rows.append({**base, "status": "ok", **res})
                    print(
                        f"trial {trial}/{trials} {model:12s} {config:16s} {res['latency_median_ms']:8.3f} ms  "
                        f"warmup {res['warmup_s']:.2f} s  rss {res['peak_rss_mb'] or 0:.0f} MB",
                        flush=True,
                    )
                except Exception as exc:
                    rows.append({**base, "status": "failed", "notes": str(exc)})
                    print(f"trial {trial}/{trials} {model:12s} {config:16s} FAILED: {exc}", flush=True)
    return rows


def print_summary(summary: List[dict]) -> None:
    print()
    for r in summary:
        if r.get("median_ms") is None:
            print(f"  {r['model']:<12} {r['config']:<16} {r['status']}: {r.get('notes') or ''}")
            continue
        sp = r.get("speedup")
        sp_txt = f"  x{sp:.2f} [{r['speedup_ci_low']:.2f}, {r['speedup_ci_high']:.2f}] vs {r['baseline']}" if sp else ""
        print(
            f"  {r['model']:<12} {r['config']:<16} {r['median_ms']:8.3f} ms [{r['ci_low_ms']:.3f}, {r['ci_high_ms']:.3f}]"
            f" n={r['trials']}  warmup {r['warmup_s']:.2f} s  rss {r['peak_rss_mb'] or 0:.0f} MB{sp_txt}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default=",".join(MODELS))
    parser.add_argument("--check", action="store_true", help="compare gradients with PyTorch")
    parser.add_argument(
        "--check-configs", default="camel_nvm,camel_opt_nvm", help="Camel configurations to check"
    )
    parser.add_argument("--rtol", type=float, default=1e-4, help="gradient tolerance relative to max |grad| (see check)")
    parser.add_argument("--check-onnx", action="store_true", help="export the training step and run it on ORT")
    parser.add_argument("--time", action="store_true", help="time training steps")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--reps", type=int, default=20)
    # FastVM's heap grows to its steady-state size over the first few dozen steps (it keeps
    # more intermediates alive than NodeVM); time the steady state, as for PyTorch.
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--threads", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    models = [m for m in args.models.split(",") if m]
    ok = True
    if args.check_onnx:
        ok = check_onnx(models, args.rtol)
        print("onnx training-step check:", "PASS" if ok else "FAIL")
    if args.check or not (args.time or args.check_onnx):
        ok = check(models, args.rtol, [c for c in args.check_configs.split(",") if c])
        print("gradient check:", "PASS" if ok else "FAIL")
    if args.time:
        rows = time_models(models, args.trials, args.reps, args.warmup, args.threads)
        summary = stats.summarize(rows, baseline="torch_eager")
        print_summary(summary)
        out = args.out or BENCH_ROOT / "results" / f"train-{time.strftime('%Y%m%d-%H%M%S')}.csv"
        out.parent.mkdir(parents=True, exist_ok=True)
        _write_csv(out, COLUMNS, rows)
        spath = out.with_name(f"{out.stem}_summary{out.suffix or '.csv'}")
        _write_csv(spath, stats.SUMMARY_COLUMNS, summary)
        meta = envinfo.write_meta(
            out,
            threads=args.threads,
            extra={"workload": "training", "trials": args.trials, "warmup": args.warmup, "reps": args.reps},
        )
        print(f"[done] wrote {out}, {spath} and {meta}")
        print(f"[note] {envinfo.NOTE}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
