"""E2 four-way training-step benchmark driver (CPU): Camel vs torch.compile vs
JAX (jit+grad) vs ORT Training, against the PyTorch eager anchor.

Interleaving follows §A.5 of the evaluation plan and ``train.py``'s design:
``--trials`` rounds; within each round every (model, config) is measured once,
in an order shuffled per round with a fixed ``--seed`` (no configuration always
runs first, and the bootstrap CIs in ``common.stats`` pair measurements by
trial). Every single measurement runs in a *fresh child process* so peak RSS
and first-call (compile) costs are not polluted by other frameworks — the same
discipline as ``train.time_models``.

Camel configurations are measured by reusing ``train.camel_trial_row`` (the
existing entry point; this script never modifies ``train.py``), and the eager
anchor reuses ``train.torch_trial_row``, so the new numbers are directly
comparable to ``results/rq4_train.csv``. The baselines come from
``baselines/torch_compile_runner.py``, ``baselines/jax_runner.py`` and
``baselines/ort_training_runner.py``.

Failure is data (§A.5): a failing (model, config) produces a ``failed`` row
whose ``notes`` carry the error tail, and the child's complete stdout+stderr is
appended to ``<out dir>/errors/<config>_<model>.log``. The round always
continues.

Environments: the measurements are subprocesses, so each framework family can
get its own interpreter — on the server that is ``venv-bench`` for torch/jax
and ``venv-orttrain`` for ORT Training (onnxruntime-training conflicts with
onnxruntime). This script itself needs only numpy/psutil.

Outputs (next to ``--out``):
  <out>                       raw per-trial rows, train.py's CSV columns
  <out stem>_summary.csv      common.stats.summarize: medians, bootstrap 95% CIs,
                              speedup vs --baseline (paired by trial when possible)
  <out stem>.meta.json        common.envinfo environment record (+ seeds, pythons)
  errors/<config>_<model>.log full verbatim output of every failed measurement
  <out stem>_capability/<model>.json   ORT Training artifact census (--capability)

Usage:
  python baselines/run_all.py --models mlp,gru,transformer --trials 10 \
      --out baselines/results/e2.csv
  python baselines/run_all.py --models mlp --configs torch_eager,torch_compile \
      --trials 2 --reps 5 --warmup 3 --out /tmp/e2-smoke.csv
  # server (two venvs, pinned cores):
  taskset -c 0-3 venv-bench/bin/python baselines/run_all.py --trials 10 \
      --python-ort ~/camel-eval/venv-orttrain/bin/python --capability \
      --out baselines/results/e2.csv
"""

from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

BENCH_ROOT = Path(__file__).resolve().parents[1]
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common import envinfo, stats  # noqa: E402
import train  # noqa: E402  (read-only reuse: CAMEL_CONFIGS, COLUMNS, trial rows in children)
from run import _write_csv  # noqa: E402

# Non-Camel configs and how to measure them. ``call`` is formatted with
# model/reps/warmup/threads and evaluated as ``<module>.<call>`` in the child.
BASELINE_RUNNERS: Dict[str, Tuple[str, str, str]] = {
    # config -> (module, call template, interpreter key)
    "torch_eager": ("train", "torch_trial_row({model!r}, {reps}, {warmup}, {threads})", "torch"),
    "torch_compile": (
        "baselines.torch_compile_runner",
        "trial_row({model!r}, 'default', {reps}, {warmup}, {threads})",
        "torch",
    ),
    "torch_compile_ro": (
        "baselines.torch_compile_runner",
        "trial_row({model!r}, 'reduce-overhead', {reps}, {warmup}, {threads})",
        "torch",
    ),
    "jax_jit": ("baselines.jax_runner", "trial_row({model!r}, {reps}, {warmup}, {threads})", "jax"),
    "ort_training": ("baselines.ort_training_runner", "trial_row({model!r}, {reps}, {warmup}, {threads})", "ort"),
}

# torch_compile_ro stays opt-in: reduce-overhead targets CUDA graphs and is a
# secondary data point on CPU.
DEFAULT_CONFIGS = ["torch_eager", "torch_compile", "jax_jit", "ort_training", *train.CAMEL_CONFIGS]

ROW_PREFIX = "ROW "


class ChildError(RuntimeError):
    def __init__(self, message: str, stdout: str, stderr: str):
        super().__init__(message)
        self.stdout = stdout
        self.stderr = stderr


def _child_code(module: str, call: str) -> str:
    return (
        f"import json, sys; sys.path.insert(0, {str(BENCH_ROOT)!r}); "
        f"import {module} as _m; print({ROW_PREFIX!r} + json.dumps(_m.{call}))"
    )


def run_child(python: str, module: str, call: str, timeout: float) -> dict:
    proc = subprocess.run(
        [python, "-c", _child_code(module, call)],
        cwd=BENCH_ROOT,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        raise ChildError(f"child exited {proc.returncode}: {proc.stderr.strip()[-400:]}", proc.stdout, proc.stderr)
    for line in reversed(proc.stdout.splitlines()):
        if line.startswith(ROW_PREFIX):
            return json.loads(line[len(ROW_PREFIX) :])
    raise ChildError(f"no {ROW_PREFIX!r} line in child output", proc.stdout, proc.stderr)


def _resolve(config: str, model: str, reps: int, warmup: int, threads: int) -> Tuple[str, str]:
    """(module, call) for one measurement."""
    if config in BASELINE_RUNNERS:
        module, template, _ = BASELINE_RUNNERS[config]
        return module, template.format(model=model, reps=reps, warmup=warmup, threads=threads)
    if config in train.CAMEL_CONFIGS:
        passes = train.CAMEL_CONFIGS[config]
        return "train", f"camel_trial_row({model!r}, {passes!r}, {reps}, {warmup}, {threads})"
    raise ValueError(f"unknown config {config!r}")


def _python_for(config: str, pythons: Dict[str, str]) -> str:
    if config in train.CAMEL_CONFIGS:
        return pythons["torch"]  # camel children need only stdlib+numpy+psutil
    return pythons[BASELINE_RUNNERS[config][2]]


def _log_error(errors_dir: Path, config: str, model: str, trial: int, exc: Exception) -> Path:
    errors_dir.mkdir(parents=True, exist_ok=True)
    path = errors_dir / f"{config}_{model}.log"
    with path.open("a") as f:
        f.write(f"===== trial {trial} {model} {config} at {time.strftime('%Y-%m-%d %H:%M:%S')} =====\n")
        f.write(f"{exc}\n")
        if isinstance(exc, ChildError):
            f.write("----- child stdout -----\n" + exc.stdout + "\n----- child stderr -----\n" + exc.stderr + "\n")
    return path


def time_models(
    models: List[str],
    configs: List[str],
    trials: int,
    reps: int,
    warmup: int,
    threads: int,
    pythons: Dict[str, str],
    seed: int,
    errors_dir: Path,
    timeout: float,
) -> List[dict]:
    """Raw rows, one per (model, config, trial), in train.py's format."""
    rng = random.Random(seed)
    rows: List[dict] = []
    for trial in range(1, trials + 1):
        for model in models:
            order = configs[:]
            rng.shuffle(order)  # shuffled per round, fixed seed: no config always runs first
            for config in order:
                base = {"model": model, "config": config, "trial": trial, "threads": threads, "warmup": warmup, "reps": reps}
                try:
                    module, call = _resolve(config, model, reps, warmup, threads)
                    res = run_child(_python_for(config, pythons), module, call, timeout)
                    rows.append({**base, "status": "ok", **res})
                    print(
                        f"trial {trial}/{trials} {model:12s} {config:16s} {res['latency_median_ms']:8.3f} ms  "
                        f"compile {res.get('compile_s') or 0:.2f} s  warmup {res.get('warmup_s') or 0:.2f} s  "
                        f"rss {res.get('peak_rss_mb') or 0:.0f} MB",
                        flush=True,
                    )
                except Exception as exc:
                    log = _log_error(errors_dir, config, model, trial, exc)
                    rows.append({**base, "status": "failed", "notes": str(exc)[:400]})
                    print(f"trial {trial}/{trials} {model:12s} {config:16s} FAILED (log: {log}): {exc}", flush=True)
    return rows


def collect_capability(models: List[str], out_dir: Path, python: str, errors_dir: Path, timeout: float) -> None:
    """ORT Training artifact census per model (the raw material of capability_notes.md)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    for model in models:
        try:
            run_child(python, "baselines.ort_training_runner", f"capability({model!r}, {str(out_dir)!r})", timeout)
            print(f"[capability] {model}: wrote {out_dir / (model + '.json')}", flush=True)
        except Exception as exc:
            _log_error(errors_dir, "ort_training_capability", model, 0, exc)
            print(f"[capability] {model}: FAILED (archived): {exc}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--configs", default=",".join(DEFAULT_CONFIGS), help="default: " + ",".join(DEFAULT_CONFIGS))
    parser.add_argument("--trials", type=int, default=10)
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--baseline", default="torch_eager", help="speedup reference config")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--resamples", type=int, default=stats.DEFAULT_RESAMPLES)
    parser.add_argument("--timeout", type=float, default=3600.0, help="per-measurement child timeout, seconds")
    parser.add_argument("--python-torch", default=sys.executable, help="interpreter for torch + camel children")
    parser.add_argument("--python-jax", default=sys.executable, help="interpreter for jax children")
    parser.add_argument("--python-ort", default=sys.executable, help="interpreter for ORT-Training children")
    parser.add_argument("--capability", action="store_true", help="also run the ORT Training artifact census first")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    models = [m for m in args.models.split(",") if m]
    configs = [c for c in args.configs.split(",") if c]
    for config in configs:
        if config not in BASELINE_RUNNERS and config not in train.CAMEL_CONFIGS:
            parser.error(f"unknown config {config!r}; known: {sorted(BASELINE_RUNNERS) + sorted(train.CAMEL_CONFIGS)}")
    out = args.out or BENCH_ROOT / "baselines" / "results" / f"e2-{time.strftime('%Y%m%d-%H%M%S')}.csv"
    out.parent.mkdir(parents=True, exist_ok=True)
    errors_dir = out.parent / "errors"
    pythons = {"torch": args.python_torch, "jax": args.python_jax, "ort": args.python_ort}

    if args.capability:
        collect_capability(models, out.with_name(out.stem + "_capability"), pythons["ort"], errors_dir, args.timeout)

    rows = time_models(models, configs, args.trials, args.reps, args.warmup, args.threads, pythons, args.seed, errors_dir, args.timeout)
    summary = stats.summarize(rows, baseline=args.baseline, resamples=args.resamples, seed=args.seed)
    train.print_summary(summary)

    _write_csv(out, train.COLUMNS, rows)
    spath = out.with_name(f"{out.stem}_summary{out.suffix or '.csv'}")
    _write_csv(spath, stats.SUMMARY_COLUMNS, summary)
    meta = envinfo.write_meta(
        out,
        threads=args.threads,
        extra={
            "workload": "training-step (E2 baselines)",
            "configs": configs,
            "trials": args.trials,
            "warmup": args.warmup,
            "reps": args.reps,
            "seed": args.seed,
            "interleave": "per-trial shuffled order, fresh child process per measurement",
            "pythons": pythons,
            "errors_dir": str(errors_dir),
        },
    )
    print(f"[done] wrote {out}, {spath} and {meta}")
    print(f"[note] {envinfo.NOTE}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
