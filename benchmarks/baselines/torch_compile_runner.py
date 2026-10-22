# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""torch.compile baseline for the E2 training-step benchmark (CPU).

Same workload and step semantics as ``train.py``: for each model the loss is
``mean(y * y)`` of the reference model's output on the benchmark input, and one
training step is ``zero_grad -> forward+loss -> backward -> SGD(lr 0.01)``.
Weights and input come from the shared ``artifacts/<model>/weights/*.npy``, so
the computation is identical to the Camel and PyTorch eager numbers already in
``results/rq4_train.csv``.

Three forms (``--modes``):

  eager            PyTorch eager, byte-for-byte the same step as
                   ``train.torch_trial_row`` (the anchor config ``torch_eager``)
  default          ``torch.compile`` (inductor) around a module computing
                   forward+loss; ``backward()`` runs the AOTAutograd-generated
                   compiled backward; ``zero_grad``/``opt.step()`` stay eager
                   (config ``torch_compile``)
  reduce-overhead  same with ``mode="reduce-overhead"`` (config
                   ``torch_compile_ro``; optional, off by default in run_all)

Timing mirrors ``train.py`` exactly: the first call (compilation) is reported
as ``compile_s``, ``warmup_s`` covers everything up to the end of the warmup
calls (compile included), and each of the ``--reps`` calls is timed
individually. Threads are pinned with ``torch.set_num_threads(threads)``
(default 4, matching the existing numbers).

This module is also the golden producer for the cross-framework checks:
``--dump-step DIR`` writes ``<model>_golden.npz`` (``__loss__`` plus every
weight after one SGD step, keyed by the harness weight names), which
``jax_runner.py`` / ``ort_training_runner.py`` compare against with
``--check --golden DIR`` (the ORT venv has no torch, so the golden must come
from an environment that has it).

Usage:
  python baselines/torch_compile_runner.py --models mlp --modes eager,default \
      --reps 5 --warmup 3 --out /tmp/tc.csv
  python baselines/torch_compile_runner.py --dump-step baselines/golden
  python baselines/torch_compile_runner.py --check --golden baselines/golden
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Dict, List

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

import train  # noqa: E402  (read-only reuse: torch_model/torch_loss/LR/COLUMNS/_latency_fields)
from common.weights import load  # noqa: E402
from run import _write_csv  # noqa: E402

LR = train.LR  # 0.01

# mode name -> config name used in the CSV (must match run_all.py).
MODES = {
    "eager": "torch_eager",
    "default": "torch_compile",
    "reduce-overhead": "torch_compile_ro",
}


def _loss_module(net):
    """Module computing forward+loss so both live inside the compiled graph."""
    import torch

    class _LossModule(torch.nn.Module):
        def __init__(self, net):
            super().__init__()
            self.net = net

        def forward(self, x):
            y = self.net(x)
            return (y * y).mean()

    return _LossModule(net)


def trial_row(model: str, mode: str, reps: int, warmup: int, threads: int) -> dict:
    """One trial of (model, mode); designed to run inside a fresh process.

    Returns the same fields as ``train.torch_trial_row`` so rows drop straight
    into the existing CSV/summary pipeline (``common.stats.summarize``).
    """
    import torch

    from common import torch_utils as tu
    from common.memory import peak_rss_self_kb

    tu.set_threads(threads)
    weights, x = load(model)
    net = train.torch_model(model, weights)
    opt = torch.optim.SGD(net.parameters(), lr=LR)
    xt = torch.from_numpy(x)

    if mode == "eager":

        def step():
            opt.zero_grad(set_to_none=True)
            train.torch_loss(net, xt).backward()
            opt.step()

    elif mode in ("default", "reduce-overhead"):
        kwargs = {} if mode == "default" else {"mode": "reduce-overhead"}
        compiled = torch.compile(_loss_module(net), **kwargs)

        def step():
            opt.zero_grad(set_to_none=True)
            compiled(xt).backward()
            opt.step()

    else:
        raise ValueError(f"unknown mode {mode!r}; choose from {sorted(MODES)}")

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
        **train._latency_fields(times),
        "peak_rss_mb": peak_rss_self_kb() / 1024.0,
    }


# ---------------------------------------------------------------- golden/check


def dump_step(model: str, out_dir: Path) -> Path:
    """Golden one-step reference: loss and updated weights (harness weight names)."""
    weights, _ = load(model)
    grads, _ = train.torch_gradients(model)  # value-matched to harness weight names
    import torch

    net = train.torch_model(model, weights)
    _, x = load(model)
    loss = float(train.torch_loss(net, torch.from_numpy(x)))
    updated = {name: (weights[name] - LR * grads[name]).astype(np.float32) for name in weights}
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{model}_golden.npz"
    np.savez(path, __loss__=np.float64(loss), **updated)
    return path


def check(model: str, golden_dir: Path, rtol: float) -> bool:
    """Re-run one eager step and compare against the golden (harness self-check)."""
    import torch

    golden = np.load(golden_dir / f"{model}_golden.npz")
    weights, x = load(model)
    net = train.torch_model(model, weights)
    opt = torch.optim.SGD(net.parameters(), lr=LR)
    xt = torch.from_numpy(x)
    opt.zero_grad(set_to_none=True)
    loss_t = train.torch_loss(net, xt)
    loss_t.backward()
    opt.step()

    _, names = train.torch_gradients(model)  # harness weight name -> torch param name (.T = transposed)
    params = dict(net.named_parameters())
    ok = True
    ref_loss = float(golden["__loss__"])
    loss_err = abs(float(loss_t) - ref_loss) / max(abs(ref_loss), 1e-12)
    ok &= loss_err <= rtol
    print(f"== {model} (torch eager vs golden)")
    print(f"  {'loss':12s} {'ok' if loss_err <= rtol else 'MISMATCH':8s} |diff|/|ref| = {loss_err:.2e}")
    for name in sorted(names):
        pname = names[name]
        got = params[pname[:-2] if pname.endswith(".T") else pname].detach().numpy()
        if pname.endswith(".T"):
            got = got.T
        ref = golden[name]
        scale = float(np.max(np.abs(weights[name]))) or 1.0
        err = float(np.max(np.abs(got - ref))) / scale
        status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
        ok &= status == "ok"
        print(f"  {name:12s} {status:8s} max|diff|/max|w| = {err:.2e}")
    return ok


# ---------------------------------------------------------------------- cli


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--modes", default="eager,default", help=f"comma list of: {', '.join(MODES)}")
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--out", type=Path, help="write rows to this CSV (train.py columns)")
    parser.add_argument("--dump-step", type=Path, metavar="DIR", help="write <model>_golden.npz per model and exit")
    parser.add_argument("--check", action="store_true", help="compare one eager step against the golden")
    parser.add_argument("--golden", type=Path, default=BENCH_ROOT / "baselines" / "golden")
    parser.add_argument("--rtol", type=float, default=1e-4)
    args = parser.parse_args()
    models = [m for m in args.models.split(",") if m]

    if args.dump_step:
        for model in models:
            path = dump_step(model, args.dump_step)
            print(f"[golden] {model}: {path}")
        return 0
    if args.check:
        ok = all(check(m, args.golden, args.rtol) for m in models)
        print("torch golden check:", "PASS" if ok else "FAIL")
        return 0 if ok else 1

    rows: List[dict] = []
    modes = [m for m in args.modes.split(",") if m]
    for model in models:
        for mode in modes:
            config = MODES[mode]
            base = {"model": model, "config": config, "trial": 1, "threads": args.threads, "warmup": args.warmup, "reps": args.reps}
            try:
                res = trial_row(model, mode, args.reps, args.warmup, args.threads)
                rows.append({**base, "status": "ok", **res})
                print(f"{model:12s} {config:16s} {res['latency_median_ms']:8.3f} ms  compile {res['compile_s']:.2f} s")
            except Exception as exc:
                rows.append({**base, "status": "failed", "notes": str(exc)})
                print(f"{model:12s} {config:16s} FAILED: {exc}")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        _write_csv(args.out, train.COLUMNS, rows)
        print(f"[done] wrote {args.out}")
    else:
        print(json.dumps(rows, indent=2, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
