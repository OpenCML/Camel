"""Benchmark driver: runs every (model, configuration) pair and writes a CSV.

Pipeline:
  1. Generate deterministic weights/inputs (common/weights.py) if missing.
  2. Export stage: for each ONNX-consuming config, export the model once in a
     fresh process (common/export.py) and record ``export_s``.
  3. Measure stage: each (model, config) runs in a fresh process
     (common/worker.py) with pinned thread counts; the worker also checks the
     output against the NumPy reference (``max_abs_err`` / ``agree`` columns).
  4. Write one CSV row per pair, including skipped/failed rows with a reason.

Repeated trials (``--trials N``, N > 1): after the export stage of a model, run
N rounds; each round measures every config once, in a round-robin order that
is shuffled per round (``--seed``) so slow drift in machine load does not bias
one config. The raw CSV gets a ``trial`` column (one row per trial), and
``<out>_summary.csv`` holds per-config medians with bootstrap 95% CIs and a
speedup vs ``--baseline`` (see common/stats.py).

Example:
  python run.py --models mlp,lenet,gru,transformer --threads 4 \
      --warmup 10 --reps 50 --out results/run.csv
  python run.py --trials 10 --out results/paper.csv   # + results/paper_summary.csv
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import random
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional

BENCH_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_ROOT))

from common import configs as cfgs  # noqa: E402
from common import stats  # noqa: E402
from common.models import MODELS  # noqa: E402
from common.weights import generate  # noqa: E402

COLUMNS = [
    "model",
    "config",
    "status",
    "threads",
    "warmup",
    "reps",
    "batch",
    "export_s",
    "import_s",
    "load_s",
    "compile_s",
    "warmup_s",
    "latency_median_ms",
    "latency_p10_ms",
    "latency_p90_ms",
    "latency_mean_ms",
    "throughput_sps",
    "peak_rss_mb",
    "max_abs_err",
    "agree",
    "notes",
]


def _run_child(script: str, argv: List[str], env: Dict[str, str], timeout: int) -> dict:
    """Run a helper script in a fresh interpreter and parse its RESULT line."""
    cmd = [sys.executable, str(BENCH_ROOT / "common" / script), *argv]
    proc = subprocess.run(cmd, cwd=BENCH_ROOT, env=env, capture_output=True, text=True, timeout=timeout)
    for line in proc.stdout.splitlines():
        if line.startswith("RESULT "):
            return json.loads(line[len("RESULT ") :])
    tail = (proc.stderr or proc.stdout).strip().splitlines()[-3:]
    raise RuntimeError(f"exit {proc.returncode}: {' | '.join(tail)}"[:400])


def _fmt(v) -> str:
    if v is None:
        return ""
    if isinstance(v, float):
        return f"{v:.6g}"
    return str(v)


def _write_csv(path: Path, columns: List[str], rows: List[dict]) -> None:
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: _fmt(r.get(k)) for k in columns})


def _export_stage(model: str, configs: List[str], args, env: Dict[str, str]):
    """Export ``model`` once per ONNX exporter; returns (export_s, export_err)."""
    export_s: Dict[str, Optional[float]] = {}
    export_err: Dict[str, str] = {}
    for config in configs:
        exporter = cfgs.CONFIGS[config].exporter
        if exporter not in ("torch", "tf", "camel") or exporter in export_s or not cfgs.applies(config, model):
            continue
        if args.no_export and cfgs.onnx_path(model, exporter).exists():
            export_s[exporter] = None
            continue
        print(f"[export] {model} via {exporter} ...", flush=True)
        try:
            res = _run_child(
                "export.py",
                ["--model", model, "--exporter", exporter, "--threads", str(args.threads)],
                env,
                args.timeout,
            )
            export_s[exporter] = res["export_s"]
        except Exception as exc:
            export_s[exporter] = None
            export_err[exporter] = str(exc)
            print(f"[export] {model} via {exporter} FAILED: {exc}", flush=True)
    return export_s, export_err


def _measure(
    model: str,
    config: str,
    args,
    env: Dict[str, str],
    export_s: Dict[str, Optional[float]],
    export_err: Dict[str, str],
    tag: str = "",
) -> dict:
    """Measure one (model, config) in a fresh process; never raises."""
    base = {
        "model": model,
        "config": config,
        "threads": args.threads,
        "warmup": args.warmup,
        "reps": args.reps,
        "batch": MODELS[model].batch,
    }
    exporter = cfgs.CONFIGS[config].exporter
    reason = cfgs.skip_reason(config, model) or export_err.get(exporter)
    if reason:
        status = "skipped" if exporter not in export_err else "failed"
        print(f"[{status}]{tag} {model}/{config}: {reason}", flush=True)
        return {**base, "status": status, "notes": reason}
    print(f"[run]{tag} {model}/{config} ...", end=" ", flush=True)
    try:
        res = _run_child(
            "worker.py",
            [
                "--model", model,
                "--config", config,
                "--threads", str(args.threads),
                "--interop", str(args.interop),
                "--warmup", str(args.warmup),
                "--reps", str(args.reps),
            ],
            env,
            args.timeout,
        )
        res.update(base)
        if exporter in export_s:
            res["export_s"] = export_s[exporter]
        print(
            f"median {res['latency_median_ms']:.3f} ms, compile {res['compile_s'] or 0:.2f} s, "
            f"rss {res['peak_rss_mb'] or 0:.0f} MB, agree={res['agree']}",
            flush=True,
        )
        return res
    except Exception as exc:
        print(f"FAILED: {exc}", flush=True)
        return {**base, "status": "failed", "notes": str(exc)}


def summary_path(out: Path) -> Path:
    """``results/x.csv`` -> ``results/x_summary.csv``."""
    return out.with_name(f"{out.stem}_summary{out.suffix or '.csv'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", default=",".join(MODELS), help=f"comma list from {list(MODELS)}")
    ap.add_argument(
        "--configs",
        default=",".join(cfgs.DEFAULT_CONFIGS),
        help=f"comma list from {list(cfgs.CONFIGS)} (default: %(default)s)",
    )
    ap.add_argument("--threads", type=int, default=4, help="intra-op threads for every runtime")
    ap.add_argument("--interop", type=int, default=1, help="inter-op threads (torch/TF/ORT)")
    ap.add_argument("--warmup", type=int, default=10)
    ap.add_argument("--reps", type=int, default=50)
    ap.add_argument("--out", default=None, help="CSV path (default results/<timestamp>.csv)")
    ap.add_argument("--timeout", type=int, default=1800, help="per-subprocess timeout in seconds")
    ap.add_argument("--no-export", action="store_true", help="reuse existing torch/tf/camel .onnx files")
    ap.add_argument(
        "--trials",
        type=int,
        default=1,
        help="measurement rounds per model; > 1 adds a `trial` column and writes <out>_summary.csv",
    )
    ap.add_argument("--seed", type=int, default=0, help="seed for the per-round config order and the bootstrap")
    ap.add_argument(
        "--baseline", default="torch_eager", help="config the summary's speedup column is relative to"
    )
    ap.add_argument("--resamples", type=int, default=stats.DEFAULT_RESAMPLES, help="bootstrap resamples")
    args = ap.parse_args()

    models = [m for m in args.models.split(",") if m]
    configs = [c for c in args.configs.split(",") if c]
    for name in configs:
        if name not in cfgs.CONFIGS:
            ap.error(f"unknown config {name!r}; choose from {list(cfgs.CONFIGS)}")
    for name in models:
        if name not in MODELS:
            ap.error(f"unknown model {name!r}; choose from {list(MODELS)}")
    if args.trials < 1:
        ap.error("--trials must be >= 1")
    if args.baseline not in cfgs.CONFIGS:
        ap.error(f"unknown baseline {args.baseline!r}; choose from {list(cfgs.CONFIGS)}")

    out = Path(args.out) if args.out else BENCH_ROOT / "results" / f"{dt.datetime.now():%Y%m%d-%H%M%S}.csv"
    if not out.is_absolute():
        out = BENCH_ROOT / out
    out.parent.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.update(cfgs.thread_env(args.threads, args.interop))
    env["PYTHONPATH"] = str(BENCH_ROOT) + os.pathsep + env.get("PYTHONPATH", "")

    rng = random.Random(args.seed)
    rows: List[dict] = []
    for model in models:
        generate(MODELS[model])
        # Export stage: one fresh process per (model, exporter), once per model.
        export_s, export_err = _export_stage(model, configs, args, env)

        # Measure stage: one fresh process per (model, config) per trial.
        applicable = [c for c in configs if cfgs.applies(c, model)]
        if args.trials == 1:
            for config in applicable:
                rows.append(_measure(model, config, args, env, export_s, export_err))
            continue
        for trial in range(1, args.trials + 1):
            order = list(applicable)
            rng.shuffle(order)
            print(f"[trial {trial}/{args.trials}] {model}: {', '.join(order)}", flush=True)
            for config in order:
                row = _measure(model, config, args, env, export_s, export_err, tag=f"[t{trial}]")
                row["trial"] = trial
                rows.append(row)

    if args.trials == 1:
        _write_csv(out, COLUMNS, rows)
        print(f"[done] wrote {len(rows)} rows to {out}")
    else:
        # Raw rows sorted by (model, config order, trial) for readability.
        pos = {c: i for i, c in enumerate(configs)}
        rows.sort(key=lambda r: (models.index(r["model"]), pos[r["config"]], r["trial"]))
        _write_csv(out, COLUMNS[:2] + ["trial"] + COLUMNS[2:], rows)
        summary = stats.summarize(rows, baseline=args.baseline, resamples=args.resamples, seed=args.seed)
        spath = summary_path(out)
        _write_csv(spath, stats.SUMMARY_COLUMNS, summary)
        print(f"[done] wrote {len(rows)} rows to {out}")
        print(f"[done] wrote {len(summary)} summary rows to {spath}")
        for r in summary:
            if r.get("median_ms") is None:
                print(f"  {r['model']:<12} {r['config']:<16} {r['status']}: {r.get('notes') or ''}")
                continue
            sp = r.get("speedup")
            sp_txt = f"  x{sp:.2f} [{r['speedup_ci_low']:.2f}, {r['speedup_ci_high']:.2f}]" if sp is not None else ""
            print(
                f"  {r['model']:<12} {r['config']:<16} {r['median_ms']:.4g} ms "
                f"[{r['ci_low_ms']:.4g}, {r['ci_high_ms']:.4g}] n={r['trials']}{sp_txt}"
                + (f"  ({r['notes']})" if r.get("notes") else "")
            )
    bad = [r for r in rows if r.get("status") == "failed" or r.get("agree") == "NO"]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
