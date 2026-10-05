"""D13: the repaired training benchmark, end to end, with its evidence folder.

One command runs, in this order:

  1. common/weights.py        generate the benchmark weights and inputs
  2. reconcile_train.py       operator-count reconciliation and K-step agreement with PyTorch,
                              under the stateful protocol and the stateless one (where it must
                              report the removed backward pass)
  3. train.py --time          step times under the stateful protocol, interleaved trials
  4. train.py --time --stateless   the earlier protocol, labelled "updates not consumed"

and writes under ``--out``:

  raw/                         every Camel run (one file each) and the console log of each stage
  summary/reconciliation.csv   verdict per model x config x protocol (+ opcounts.csv)
  evidence/<model>/            the graph after std::macro and the analysis of its gradient
                               operators (which are unused and why), written by reconcile_train.py
  summary/train.csv            model, config, trial, ms_per_step ... for pairs whose
                               reconciliation verdict is PASS (and the PyTorch reference)
  summary/train_unreconciled.csv   the same columns for pairs whose verdict is not PASS;
                               these are NOT training-step timings of reconciled work
  summary/train_stateless.csv  the stateless protocol's rows (updates not consumed)
  summary/train_summary.csv    median per model x config x table, ratio to PyTorch eager
  summary/rows_*.csv           train.py's full per-trial rows and its own summaries
  meta.json                    environment, versions, command lines, start and end time

``ms_per_step`` is the median of the trial's individually timed steps; ``span_ms_per_step``
is the median time between consecutive step reports (step + report), an independent check
that the step's work ran inside its timer.

Timings on a shared or virtual machine are indicative only; ``--timing-label`` is copied
into every timing row and into meta.json.

Usage:
  .venv/bin/python d13_train_eval.py --out results/d13
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Dict, List

BENCH_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_ROOT))

import train  # noqa: E402
from common import envinfo, stats  # noqa: E402

TIMING_COLUMNS = [
    "model", "config", "trial", "ms_per_step", "span_ms_per_step", "peak_rss_mb",
    "protocol", "reconciled", "status", "notes", "label",
]
SUMMARY_COLUMNS = ["table", "model", "config", "trials", "median_ms", "min_ms", "max_ms", "ratio_vs_torch_eager", "label"]


def now() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def load_avg():
    """1/5/15-minute load average: how busy the machine was around a stage (None on Windows)."""
    return list(os.getloadavg()) if hasattr(os, "getloadavg") else None


def run_stage(name: str, cmd: List[str], raw: Path, commands: List[dict]) -> int:
    """Runs one stage, copying its console output to raw/console_<name>.log."""
    log = raw / f"console_{name}.log"
    started, load = now(), load_avg()
    print(f"[d13] {name}: {' '.join(shlex.quote(c) for c in cmd)}", flush=True)
    with log.open("w") as f:
        f.write(f"# cmd: {' '.join(shlex.quote(c) for c in cmd)}\n# start: {started}\n")
        f.flush()
        proc = subprocess.Popen(cmd, cwd=BENCH_ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for line in proc.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            f.write(line)
        code = proc.wait()
        f.write(f"# exit: {code}\n# end: {now()}\n")
    commands.append(
        {"stage": name, "cmd": cmd, "exit": code, "start": started, "end": now(), "console": str(log.name),
         "load_avg_start": load, "load_avg_end": load_avg()}
    )
    return code


def read_rows(path: Path) -> List[dict]:
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def write_rows(path: Path, columns: List[str], rows: List[dict]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def timing_row(r: dict, label: str) -> dict:
    return {
        "model": r["model"], "config": r["config"], "trial": r["trial"],
        "ms_per_step": r.get("latency_median_ms", ""), "span_ms_per_step": r.get("span_median_ms", ""),
        "peak_rss_mb": r.get("peak_rss_mb", ""), "protocol": r.get("protocol", ""),
        "reconciled": r.get("reconciled", ""), "status": r.get("status", ""), "notes": r.get("notes", ""),
        "label": label,
    }


def summarize(table: str, rows: List[dict], torch_rows: List[dict], label: str) -> List[dict]:
    """Median over trials of ms_per_step per (model, config); ratio = config / PyTorch eager."""
    def medians(rs: List[dict]) -> Dict[tuple, List[float]]:
        out: Dict[tuple, List[float]] = {}
        for r in rs:
            if r["status"] == "ok" and r["ms_per_step"] not in ("", None):
                out.setdefault((r["model"], r["config"]), []).append(float(r["ms_per_step"]))
        return out

    base = {model: stats.median(v) for (model, _), v in medians(torch_rows).items()}
    out = []
    for (model, config), vals in medians(rows).items():
        med = stats.median(vals)
        out.append(
            {
                "table": table, "model": model, "config": config, "trials": len(vals),
                "median_ms": f"{med:.4f}", "min_ms": f"{min(vals):.4f}", "max_ms": f"{max(vals):.4f}",
                "ratio_vs_torch_eager": f"{med / base[model]:.3f}" if model in base else "",
                "label": label,
            }
        )
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--models", default="mlp,lenet,gru,transformer")
    ap.add_argument("--configs", default=",".join([train.TORCH_CONFIG, *train.CAMEL_CONFIGS, train.ORT_CONFIG]))
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=40)
    ap.add_argument("--reps", type=int, default=100, help="timed steps (K); the loop runs warmup + reps consecutive steps")
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--tol", type=float, default=1e-4)
    ap.add_argument("--timing-label", default=envinfo.NOTE, help="written into every timing row and meta.json")
    ap.add_argument("--meta", action="append", default=[], metavar="KEY=VALUE", help="extra meta.json entries")
    ap.add_argument("--skip-timing", action="store_true", help="reconciliation only")
    args = ap.parse_args()

    out = args.out.resolve()
    raw, summary = out / "raw", out / "summary"
    raw.mkdir(parents=True, exist_ok=True)
    summary.mkdir(parents=True, exist_ok=True)
    started = now()
    commands: List[dict] = []
    py = sys.executable
    common = ["--warmup", str(args.warmup), "--reps", str(args.reps), "--threads", str(args.threads)]

    run_stage("weights", [py, "common/weights.py", "--models", args.models], raw, commands)
    recon_code = run_stage(
        "reconcile",
        [py, "reconcile_train.py", "--models", args.models, "--configs", args.configs, *common,
         "--reps0", str(max(args.reps // 2, 1)), "--tol", str(args.tol), "--out", str(out)],
        raw, commands,
    )
    recon = summary / "reconciliation.csv"

    if not args.skip_timing:
        timed = ["--trials", str(args.trials), *common, "--models", args.models, "--raw-dir", str(raw)]
        run_stage(
            "time_stateful",
            [py, "train.py", "--time", "--configs", args.configs, "--reconciliation", str(recon),
             "--include-unreconciled", *timed, "--out", str(summary / "rows_stateful.csv")],
            raw, commands,
        )
        run_stage(
            "time_stateless",
            [py, "train.py", "--time", "--stateless", "--configs", args.configs, *timed,
             "--out", str(summary / "rows_stateless.csv")],
            raw, commands,
        )
        label = args.timing_label
        stateful = [timing_row(r, label) for r in read_rows(summary / "rows_stateful.csv")]
        stateless = [timing_row(r, label) for r in read_rows(summary / "rows_stateless.csv")]
        torch_rows = [r for r in stateful if r["config"] == train.TORCH_CONFIG]
        passed = [r for r in stateful if r["config"] == train.TORCH_CONFIG or r["reconciled"] == "PASS"]
        failed = [r for r in stateful if r not in passed]
        write_rows(summary / "train.csv", TIMING_COLUMNS, passed)
        write_rows(summary / "train_unreconciled.csv", TIMING_COLUMNS, failed)
        write_rows(summary / "train_stateless.csv", TIMING_COLUMNS, stateless)
        write_rows(
            summary / "train_summary.csv",
            SUMMARY_COLUMNS,
            summarize("train.csv (reconciled)", passed, torch_rows, label)
            + summarize("train_unreconciled.csv (verdict not PASS)", failed, torch_rows, label)
            + summarize("train_stateless.csv (updates not consumed)", stateless,
                        [r for r in stateless if r["config"] == train.TORCH_CONFIG], label),
        )

    extra = {
        "task": "D13",
        "start": started,
        "end": now(),
        "models": args.models.split(","),
        "configs": {c: " ".join(train.config_commands()[c]) for c in args.configs.split(",")},
        "protocol": {"warmup": args.warmup, "reps": args.reps, "trials": args.trials, "tolerance": args.tol},
        "timing_label": args.timing_label,
        "stages": commands,
        "argv": sys.argv,
    }
    for item in args.meta:
        key, _, value = item.partition("=")
        extra[key] = value
    (out / "meta.json").write_text(json.dumps(envinfo.collect(args.threads, extra), indent=2) + "\n")
    print(f"[d13] wrote {out} (reconciliation exit {recon_code}: {'all stateful rows PASS' if recon_code == 0 else 'some stateful rows FAIL, see summary/reconciliation.csv'})")
    return recon_code


if __name__ == "__main__":
    sys.exit(main())
