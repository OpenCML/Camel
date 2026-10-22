# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E5 server re-measurement driver (MLSys 2027 eval, §A.5 protocol).

One command on the 8x3090 server produces every performance table of the paper:

  training-step latency + peak RSS   train.py --time methodology (4 Camel configs + torch_eager anchor)
  inference latency + peak RSS       run.py methodology (fresh worker process per measurement, agree gate)
  ONNX export time                   results/measure_export.py methodology (common/export.py per pair)
  memory comparison                  the peak_rss columns of the above, ratio vs the torch_eager baseline

Run on the server (paper protocol, defaults are the paper configuration):
  taskset -c 0-3 ~/camel-eval/venv-bench/bin/python benchmarks/e5_rerun.py

Small verification run:
  taskset -c 0-3 ~/camel-eval/venv-bench/bin/python benchmarks/e5_rerun.py \
      --models mlp --trials 2 --train-reps 5 --train-warmup 3 \
      --infer-reps 5 --infer-warmup 2 --export-rounds 2 --out-dir /tmp/e5-smoke

§A.5 discipline implemented here:
  * interleaving: within every trial the (model, config) order is shuffled
    (random.Random(--seed)); a speedup is computed only from measurements taken
    in the same trial (common.stats.summarize pairs by trial).
  * load monitoring (common/loadmon.py) wraps every trial: the busy fraction of
    the CPUs outside the pinned set is sampled; a window rising more than
    --busy-delta above its pre-trial baseline is discarded and rerun, up to
    --retries attempts; after that the rows are kept and marked contaminated
    (notes column, load.csv action=kept-contaminated). Every attempt — accepted,
    discarded or kept — is logged in load.csv; discarded details in discarded.log.
  * environment record in meta.json: CPU model, AVX2/AVX-512 flags, per-core
    governor and frequency snapshot (read-only: no sudo on the server, so the
    governor is recorded, not locked), GPU inventory via nvidia-smi, library
    versions, Camel commit and binary (common/envinfo.py).
  * failure is data: a failed measurement keeps a `failed` row and its full
    stdout+stderr is archived under errors/.

Outputs under --out-dir (default $CAMEL_E5_OUT, else ~/camel-eval/e5):
  train.csv / train_summary.csv    raw per-trial rows + median/CI/speedup
  infer.csv / infer_summary.csv
  export.csv / export_summary.csv
  memory.csv                       peak RSS per (workload, model, config) + reduction vs baseline
  meta.json                        environment + protocol record
  load.csv                         one row per trial attempt (the contamination audit trail)
  discarded.log                    load statistics of discarded (contaminated) attempts
  errors/                          verbatim output of every failed measurement

Note: venv-bench on the server has no TensorFlow, so the default inference
configs and exporters exclude the TF ones; pass --infer-configs/--exporters
explicitly in a TF-equipped environment to restore run.py's full matrix.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import random
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Optional, Sequence, Tuple

BENCH_ROOT = Path(__file__).resolve().parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common import configs as cfgs  # noqa: E402
from common import envinfo, stats  # noqa: E402
from common.loadmon import LoadMonitor  # noqa: E402
from common.models import MODELS  # noqa: E402
from common.weights import generate  # noqa: E402
import train  # noqa: E402  (read-only reuse: CAMEL_CONFIGS, COLUMNS; children run train.*_trial_row)
import run as runpy  # noqa: E402  (read-only reuse: COLUMNS, _write_csv, _export_stage)
from baselines.run_all import ChildError, _log_error, _resolve, run_child  # noqa: E402

TRAIN_CONFIGS = ["torch_eager", *train.CAMEL_CONFIGS]
# run.py's DEFAULT_CONFIGS minus the TF configs (no TensorFlow in the server venv).
INFER_CONFIGS = ["torch_eager", "torch_compile", "torch_onnx_ort", "torch_nn_gru", "camel_native", "camel_opt", "camel_onnx_ort"]
EXPORTERS = ["torch", "camel"]

LOAD_COLUMNS = [
    "block",
    "trial",
    "attempt",
    "time",
    "pre_load1",
    "pre_load5",
    "pre_load15",
    "baseline_busy",
    "busy_max",
    "busy_mean",
    "busy_n",
    "load1_max",
    "gpu_max",
    "contaminated",
    "action",
    "reasons",
]

MEMORY_COLUMNS = [
    "workload",
    "model",
    "config",
    "trials",
    "peak_rss_mb",
    "baseline",
    "baseline_rss_mb",
    "rss_reduction",
    "reduction_ci_low",
    "reduction_ci_high",
]

EXPORT_COLUMNS = ["model", "config", "trial", "status", "threads", "latency_median_ms", "notes"]


# ---------------------------------------------------------------------------
# Measurement units (one child process per unit; failure is data, never raises)
# ---------------------------------------------------------------------------


def train_exec(args, errors_dir: Path):
    def exec_one(unit: Tuple[str, str], trial: int) -> dict:
        model, config = unit
        base = {
            "model": model,
            "config": config,
            "trial": trial,
            "threads": args.threads,
            "warmup": args.train_warmup,
            "reps": args.train_reps,
        }
        try:
            module, call = _resolve(config, model, args.train_reps, args.train_warmup, args.threads)
            res = run_child(args.python, module, call, args.timeout)
            print(
                f"  train t{trial} {model:12s} {config:16s} {res['latency_median_ms']:8.3f} ms  "
                f"warmup {res.get('warmup_s') or 0:.2f} s  rss {res.get('peak_rss_mb') or 0:.0f} MB",
                flush=True,
            )
            return {**base, "status": "ok", **res}
        except Exception as exc:
            log = _log_error(errors_dir, f"train_{config}", model, trial, exc)
            print(f"  train t{trial} {model:12s} {config:16s} FAILED (log: {log}): {exc}", flush=True)
            return {**base, "status": "failed", "notes": str(exc)[:400]}

    return exec_one


def _run_worker_full(argv: List[str], env: Dict[str, str], timeout: float) -> dict:
    """run.py's worker invocation (same script, same argv) but keeps full output for errors/."""
    cmd = [sys.executable, str(BENCH_ROOT / "common" / "worker.py"), *argv]
    proc = subprocess.run(cmd, cwd=BENCH_ROOT, env=env, capture_output=True, text=True, timeout=timeout)
    for line in proc.stdout.splitlines():
        if line.startswith("RESULT "):
            return json.loads(line[len("RESULT ") :])
    tail = (proc.stderr or proc.stdout).strip().splitlines()[-3:]
    raise ChildError(f"worker exit {proc.returncode}: {' | '.join(tail)}"[:400], proc.stdout, proc.stderr)


def infer_exec(args, env: Dict[str, str], export_s: Dict[str, Dict[str, Optional[float]]],
               export_err: Dict[str, Dict[str, str]], errors_dir: Path):
    ns = SimpleNamespace(threads=args.threads, warmup=args.infer_warmup, reps=args.infer_reps, interop=args.interop)

    def exec_one(unit: Tuple[str, str], trial: int) -> dict:
        model, config = unit
        base = {
            "model": model,
            "config": config,
            "trial": trial,
            "threads": ns.threads,
            "warmup": ns.warmup,
            "reps": ns.reps,
            "batch": MODELS[model].batch,
        }
        exporter = cfgs.CONFIGS[config].exporter
        reason = cfgs.skip_reason(config, model) or export_err[model].get(exporter)
        if reason:
            status = "skipped" if exporter not in export_err[model] else "failed"
            print(f"  infer t{trial} {model:12s} {config:16s} {status}: {reason}", flush=True)
            return {**base, "status": status, "notes": reason}
        argv = [
            "--model", model,
            "--config", config,
            "--threads", str(ns.threads),
            "--interop", str(ns.interop),
            "--warmup", str(ns.warmup),
            "--reps", str(ns.reps),
        ]
        try:
            res = _run_worker_full(argv, env, args.timeout)
            res.update(base)
            if exporter in export_s[model]:
                res["export_s"] = export_s[model][exporter]
            print(
                f"  infer t{trial} {model:12s} {config:16s} {res['latency_median_ms']:8.3f} ms  "
                f"rss {res.get('peak_rss_mb') or 0:.0f} MB  agree={res.get('agree')}",
                flush=True,
            )
            return res
        except Exception as exc:
            log = _log_error(errors_dir, f"infer_{config}", model, trial, exc)
            print(f"  infer t{trial} {model:12s} {config:16s} FAILED (log: {log}): {exc}", flush=True)
            return {**base, "status": "failed", "notes": str(exc)[:400]}

    return exec_one


def export_exec(args, env: Dict[str, str], errors_dir: Path):
    def exec_one(unit: Tuple[str, str], trial: int) -> dict:
        model, exporter = unit
        base = {"model": model, "config": f"{exporter}_export", "trial": trial, "threads": args.threads}
        cmd = [
            args.python,
            str(BENCH_ROOT / "common" / "export.py"),
            "--model", model,
            "--exporter", exporter,
            "--threads", str(args.threads),
        ]
        try:
            proc = subprocess.run(cmd, cwd=BENCH_ROOT, env=env, capture_output=True, text=True, timeout=args.timeout)
            for line in proc.stdout.splitlines():
                if line.startswith("RESULT "):
                    export_s = float(json.loads(line[len("RESULT ") :])["export_s"])
                    print(f"  export t{trial} {model:12s} {exporter:8s} {export_s * 1000:.1f} ms", flush=True)
                    return {**base, "status": "ok", "latency_median_ms": export_s * 1000.0}
            raise ChildError(
                f"export.py exited {proc.returncode}: {(proc.stderr or proc.stdout).strip()[-400:]}", proc.stdout, proc.stderr
            )
        except Exception as exc:
            log = _log_error(errors_dir, f"export_{exporter}", model, trial, exc)
            print(f"  export t{trial} {model:12s} {exporter:8s} FAILED (log: {log}): {exc}", flush=True)
            return {**base, "status": "failed", "notes": str(exc)[:400]}

    return exec_one


# ---------------------------------------------------------------------------
# Trial driver with load monitoring and discard-and-rerun
# ---------------------------------------------------------------------------


def run_block(
    block: str,
    units: List[Tuple[str, str]],
    exec_one,
    trials: int,
    retries: int,
    mon_args: dict,
    seed: int,
) -> Tuple[List[dict], List[dict], List[dict]]:
    """Run ``trials`` interleaved rounds over ``units``; returns (rows, load_rows, discarded)."""
    rng = random.Random(seed)
    rows: List[dict] = []
    load_rows: List[dict] = []
    discarded: List[dict] = []
    for trial in range(1, trials + 1):
        order = units[:]
        rng.shuffle(order)  # per-trial shuffled order: no config always runs first
        attempt = 0
        while True:
            attempt += 1
            with LoadMonitor(**mon_args) as mon:
                got = [exec_one(u, trial) for u in order]
            st = mon.stats()
            if mon.contaminated and attempt <= retries:
                action = "discarded"
            elif mon.contaminated:
                action = "kept-contaminated"
            else:
                action = "accepted"
            load = st["baseline_load"] or (None, None, None)
            load_rows.append(
                {
                    "block": block,
                    "trial": trial,
                    "attempt": attempt,
                    "time": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
                    "pre_load1": load[0],
                    "pre_load5": load[1],
                    "pre_load15": load[2],
                    "baseline_busy": st["baseline_busy"],
                    "busy_max": st["busy"].get("max"),
                    "busy_mean": st["busy"].get("mean"),
                    "busy_n": st["busy"].get("n", 0),
                    "load1_max": st["load1"].get("max"),
                    "gpu_max": st["gpu"].get("max"),
                    "contaminated": st["contaminated"],
                    "action": action,
                    "reasons": "; ".join(st["reasons"]),
                }
            )
            print(
                f"[load] {block} trial {trial} attempt {attempt}: baseline_busy={st['baseline_busy']} "
                f"busy_max={st['busy'].get('max')} pre_load1={load[0]} -> {action}"
                + (f" ({'; '.join(st['reasons'])})" if st["reasons"] else ""),
                flush=True,
            )
            if action == "discarded":
                discarded.append({"block": block, "trial": trial, "attempt": attempt, "stats": st})
                continue
            if action == "kept-contaminated":
                mark = f"contaminated window kept after {retries} retries"
                for r in got:
                    r["notes"] = "; ".join(x for x in (r.get("notes"), mark) if x)
            rows.extend(got)
            break
    return rows, load_rows, discarded


# ---------------------------------------------------------------------------
# Memory table (peak RSS columns of train/infer raw rows, reduction vs baseline)
# ---------------------------------------------------------------------------


def memory_table(datasets: Sequence[Tuple[str, List[dict], str]]) -> List[dict]:
    out: List[dict] = []
    for workload, rows, baseline in datasets:
        rss: Dict[Tuple[str, str], Dict[str, float]] = {}
        for r in rows:
            if r.get("status") != "ok":
                continue
            try:
                v = float(r.get("peak_rss_mb"))
            except (TypeError, ValueError):
                continue
            rss.setdefault((str(r["model"]), str(r["config"])), {})[str(r.get("trial") or 0)] = v
        for (model, config), by_trial in rss.items():
            vals = list(by_trial.values())
            row: Dict[str, object] = {
                "workload": workload,
                "model": model,
                "config": config,
                "trials": len(vals),
                "peak_rss_mb": sum(vals) / len(vals),
                "baseline": baseline,
            }
            base = rss.get((model, baseline), {})
            if base and config != baseline:
                common = sorted(set(base) & set(by_trial))
                paired = len(common) == len(base) == len(by_trial)
                num = [base[t] for t in common] if paired else list(base.values())
                den = [by_trial[t] for t in common] if paired else vals
                seed_key = sum(map(ord, workload + model + config))
                row["baseline_rss_mb"] = sum(base.values()) / len(base)
                row["rss_reduction"], row["reduction_ci_low"], row["reduction_ci_high"] = stats.ratio_ci(
                    num, den, seed=seed_key, paired=paired
                )
            out.append(row)
    return out


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def print_summary(summary: List[dict]) -> None:
    for r in summary:
        if r.get("median_ms") is None:
            print(f"  {r['model']:<12} {r['config']:<16} {r['status']}: {r.get('notes') or ''}")
            continue
        sp = r.get("speedup")
        sp_txt = f"  x{sp:.2f} [{r['speedup_ci_low']:.2f}, {r['speedup_ci_high']:.2f}] vs {r['baseline']}" if sp is not None else ""
        rss = r.get("peak_rss_mb")
        print(
            f"  {r['model']:<12} {r['config']:<16} {r['median_ms']:8.3f} ms "
            f"[{r['ci_low_ms']:.3f}, {r['ci_high_ms']:.3f}] n={r['trials']}"
            + (f"  rss {rss:.0f} MB" if rss else "")
            + sp_txt
            + (f"  ({r['notes']})" if r.get("notes") else "")
        )


def sort_rows(rows: List[dict], models: List[str], configs: List[str]) -> List[dict]:
    pos = {c: i for i, c in enumerate(configs)}
    return sorted(rows, key=lambda r: (models.index(r["model"]), pos.get(r["config"], len(pos)), r.get("trial") or 0))


def default_out_dir() -> Path:
    if os.environ.get("CAMEL_E5_OUT"):
        return Path(os.environ["CAMEL_E5_OUT"])
    if (Path.home() / "camel-eval").is_dir():
        return Path.home() / "camel-eval" / "e5"
    return BENCH_ROOT / "results" / "e5"


def check_affinity(threads: int, strict: bool) -> Optional[List[int]]:
    try:
        affinity = sorted(os.sched_getaffinity(0))
    except AttributeError:
        print("[warn] os.sched_getaffinity unavailable; cannot verify CPU pinning", flush=True)
        return None
    if len(affinity) != threads:
        msg = f"CPU affinity {affinity} ({len(affinity)} CPUs) != --threads {threads}; run under `taskset -c 0-3`"
        if strict:
            raise SystemExit(f"[fatal] {msg}")
        print(f"[warn] {msg}", flush=True)
    return affinity


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", default="mlp,gru,transformer,lenet")
    ap.add_argument("--blocks", default="train,infer,export", help="comma list from train,infer,export")
    ap.add_argument("--trials", type=int, default=10, help="interleaved rounds for train and infer (paper: 10)")
    ap.add_argument("--export-rounds", type=int, default=None, help="export timing rounds (default: --trials)")
    ap.add_argument("--train-reps", type=int, default=20)
    ap.add_argument("--train-warmup", type=int, default=40)
    ap.add_argument("--infer-reps", type=int, default=50)
    ap.add_argument("--infer-warmup", type=int, default=10)
    ap.add_argument("--infer-configs", default=",".join(INFER_CONFIGS), help="default: run.py defaults minus TF (no TF in server venv)")
    ap.add_argument("--exporters", default=",".join(EXPORTERS))
    ap.add_argument("--baseline", default="torch_eager", help="speedup/RSS reference config for train and infer")
    ap.add_argument("--threads", type=int, default=4, help="intra-op threads; must match the taskset CPU count")
    ap.add_argument("--interop", type=int, default=1)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--resamples", type=int, default=stats.DEFAULT_RESAMPLES)
    ap.add_argument("--timeout", type=float, default=3600.0, help="per-measurement child timeout, seconds")
    ap.add_argument("--retries", type=int, default=3, help="discarded-trial rerun limit before keeping a contaminated window")
    ap.add_argument("--busy-delta", type=float, default=0.15, help="other-CPU busy fraction over baseline that contaminates a trial")
    ap.add_argument("--baseline-window", type=float, default=2.0, help="seconds of pre-trial load settle sampling")
    ap.add_argument("--load-interval", type=float, default=1.0, help="load sampling interval, seconds")
    ap.add_argument("--gpu", type=int, default=None, help="also monitor this GPU's utilization (off by default; E5 is CPU-only)")
    ap.add_argument("--python", default=sys.executable, help="interpreter for the measurement children")
    ap.add_argument("--no-strict-affinity", action="store_true", help="warn instead of failing when affinity != --threads")
    ap.add_argument("--out-dir", type=Path, default=None, help="default: $CAMEL_E5_OUT or ~/camel-eval/e5")
    args = ap.parse_args()

    models = [m for m in args.models.split(",") if m]
    blocks = [b for b in args.blocks.split(",") if b]
    for name in models:
        if name not in MODELS:
            ap.error(f"unknown model {name!r}; choose from {list(MODELS)}")
    for name in blocks:
        if name not in ("train", "infer", "export"):
            ap.error(f"unknown block {name!r}")
    infer_configs = [c for c in args.infer_configs.split(",") if c]
    for name in infer_configs:
        if name not in cfgs.CONFIGS:
            ap.error(f"unknown inference config {name!r}; choose from {list(cfgs.CONFIGS)}")
    exporters = [e for e in args.exporters.split(",") if e]
    for name in exporters:
        if name not in ("torch", "tf", "camel"):
            ap.error(f"unknown exporter {name!r}")
    if args.trials < 1 or (args.export_rounds is not None and args.export_rounds < 1):
        ap.error("--trials/--export-rounds must be >= 1")
    export_rounds = args.export_rounds or args.trials

    affinity = check_affinity(args.threads, strict=not args.no_strict_affinity)
    out_dir = args.out_dir or default_out_dir()
    out_dir.mkdir(parents=True, exist_ok=True)
    errors_dir = out_dir / "errors"
    t_start = time.perf_counter()

    for model in models:
        generate(MODELS[model])  # deterministic weights/inputs (no-op when present)

    env = dict(os.environ)
    env.update(cfgs.thread_env(args.threads, args.interop))
    env["PYTHONPATH"] = str(BENCH_ROOT) + os.pathsep + env.get("PYTHONPATH", "")

    mon_args = {
        "interval": args.load_interval,
        "busy_delta": args.busy_delta,
        "baseline_window": args.baseline_window,
        "gpu_index": args.gpu,
    }

    all_load_rows: List[dict] = []
    all_discarded: List[dict] = []
    raw: Dict[str, List[dict]] = {}
    summaries: Dict[str, List[dict]] = {}

    if "train" in blocks:
        print(f"[block] train: {len(models)} models x {len(TRAIN_CONFIGS)} configs x {args.trials} trials", flush=True)
        units = [(m, c) for m in models for c in TRAIN_CONFIGS]
        rows, load_rows, discarded = run_block(
            "train", units, train_exec(args, errors_dir), args.trials, args.retries, mon_args, args.seed
        )
        all_load_rows += load_rows
        all_discarded += discarded
        raw["train"] = sort_rows(rows, models, TRAIN_CONFIGS)
        summaries["train"] = stats.summarize(raw["train"], baseline=args.baseline, resamples=args.resamples, seed=args.seed)
        print("[train summary]")
        print_summary(summaries["train"])

    if "infer" in blocks:
        print(f"[block] infer: {len(models)} models x {len(infer_configs)} configs x {args.trials} trials", flush=True)
        # One-time export stage per model (run.py's stage 2): materializes the ONNX
        # artifacts the ORT configs consume; export_err propagates to failed rows.
        ns = SimpleNamespace(threads=args.threads, timeout=args.timeout, no_export=False)
        export_s: Dict[str, Dict[str, Optional[float]]] = {}
        export_err: Dict[str, Dict[str, str]] = {}
        for model in models:
            export_s[model], export_err[model] = runpy._export_stage(model, infer_configs, ns, env)
        units = [(m, c) for m in models for c in infer_configs if cfgs.applies(c, m)]
        rows, load_rows, discarded = run_block(
            "infer",
            units,
            infer_exec(args, env, export_s, export_err, errors_dir),
            args.trials,
            args.retries,
            mon_args,
            args.seed + 1,
        )
        all_load_rows += load_rows
        all_discarded += discarded
        raw["infer"] = sort_rows(rows, models, infer_configs)
        summaries["infer"] = stats.summarize(raw["infer"], baseline=args.baseline, resamples=args.resamples, seed=args.seed)
        print("[infer summary]")
        print_summary(summaries["infer"])

    if "export" in blocks:
        print(f"[block] export: {len(models)} models x {len(exporters)} exporters x {export_rounds} rounds", flush=True)
        units = [(m, e) for m in models for e in exporters]
        rows, load_rows, discarded = run_block(
            "export", units, export_exec(args, env, errors_dir), export_rounds, args.retries, mon_args, args.seed + 2
        )
        all_load_rows += load_rows
        all_discarded += discarded
        raw["export"] = sort_rows(rows, models, [f"{e}_export" for e in exporters])
        summaries["export"] = stats.summarize(raw["export"], baseline="torch_export", resamples=args.resamples, seed=args.seed)
        print("[export summary]")
        print_summary(summaries["export"])

    # ------------------------------------------------------------------ output
    written: List[str] = []
    for block, columns in (("train", train.COLUMNS), ("infer", runpy.COLUMNS[:2] + ["trial"] + runpy.COLUMNS[2:]), ("export", EXPORT_COLUMNS)):
        if block not in raw:
            continue
        out = out_dir / f"{block}.csv"
        runpy._write_csv(out, columns, raw[block])
        spath = out.with_name(f"{out.stem}_summary{out.suffix}")
        runpy._write_csv(spath, stats.SUMMARY_COLUMNS, summaries[block])
        written += [str(out), str(spath)]

    datasets = [(w, raw[w], args.baseline) for w in ("train", "infer") if w in raw]
    if datasets:
        mrows = memory_table(datasets)
        mpath = out_dir / "memory.csv"
        runpy._write_csv(mpath, MEMORY_COLUMNS, mrows)
        written.append(str(mpath))
        print("[memory]")
        for r in mrows:
            red = f"  x{r['rss_reduction']:.2f} [{r['reduction_ci_low']:.2f}, {r['reduction_ci_high']:.2f}] less RSS vs {r['baseline']}" if r.get("rss_reduction") else ""
            print(f"  {r['workload']:<6} {r['model']:<12} {r['config']:<16} {r['peak_rss_mb']:7.0f} MB{red}")

    load_path = out_dir / "load.csv"
    runpy._write_csv(load_path, LOAD_COLUMNS, all_load_rows)
    written.append(str(load_path))
    if all_discarded:
        with (out_dir / "discarded.log").open("w") as f:
            for d in all_discarded:
                f.write(f"===== {d['block']} trial {d['trial']} attempt {d['attempt']} =====\n")
                f.write(json.dumps(d["stats"], default=list) + "\n")
        written.append(str(out_dir / "discarded.log"))

    agree_bad = [r for r in raw.get("infer", []) if r.get("agree") == "NO"]
    n_failed = sum(1 for rows in raw.values() for r in rows if r.get("status") == "failed")
    meta = envinfo.collect(
        threads=args.threads,
        extra={
            "note": (
                "E5 paper-protocol measurement (eval plan §A.5): taskset-pinned cores, interleaved "
                "trials with per-trial shuffled order, bootstrap 95% CI, load-monitored windows with "
                "discard-and-rerun. These are the paper's authoritative numbers."
            ),
            "workload": "E5 full performance tables (train / infer / export / memory)",
            "argv": sys.argv,
            "models": models,
            "blocks": blocks,
            "train_configs": TRAIN_CONFIGS,
            "infer_configs": infer_configs,
            "exporters": exporters,
            "protocol": {
                "trials": args.trials,
                "export_rounds": export_rounds,
                "train_reps": args.train_reps,
                "train_warmup": args.train_warmup,
                "infer_reps": args.infer_reps,
                "infer_warmup": args.infer_warmup,
                "seed": args.seed,
                "retries": args.retries,
                "busy_delta": args.busy_delta,
                "baseline_window": args.baseline_window,
                "load_interval": args.load_interval,
                "interleave": "per-trial shuffled (model, config) order, fresh child process per measurement",
                "contamination": "other-CPU busy fraction ( /proc/stat, affinity set excluded) > pre-trial baseline + busy_delta",
                "governor_note": "governor/frequencies recorded read-only (no sudo on the server); schedutil ramps under load",
            },
            "affinity_cpus_list": affinity,
            "python_for_children": args.python,
            "discarded_attempts": len(all_discarded),
            "kept_contaminated_trials": sum(1 for r in all_load_rows if r["action"] == "kept-contaminated"),
            "failed_measurements": n_failed,
            "agree_failures": len(agree_bad),
            "errors_dir": str(errors_dir),
            "outputs": written,
            "wall_time_s": round(time.perf_counter() - t_start, 1),
        },
    )
    meta_path = out_dir / "meta.json"
    meta_path.write_text(json.dumps(meta, indent=2, default=list) + "\n")
    written.append(str(meta_path))

    print(f"[done] wrote " + ", ".join(written), flush=True)
    if n_failed:
        print(f"[warn] {n_failed} failed measurements; verbatim output archived under {errors_dir}", flush=True)
    if all_discarded:
        print(f"[warn] {len(all_discarded)} contaminated trial attempts discarded and rerun (see load.csv/discarded.log)", flush=True)
    if agree_bad:
        print(f"[error] {len(agree_bad)} inference rows disagree with the NumPy reference (agree=NO)", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
