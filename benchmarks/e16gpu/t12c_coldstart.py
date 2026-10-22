# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T12c GPU cold start (MLSys 2027 eval; e8_cold_start.py's GPU counterpart).

Wall time from "fresh process" to the FIRST completed training step, for:

  ort           import onnxruntime/onnx -> onnx.load(graph) [read+parse]
                -> InferenceSession(CUDA EP, CPU fallback disabled) [compile]
                -> first session.run [first step]   (all three segments timed)
  torch_eager   import torch -> build reference model + .cuda() + SGD
                -> first training step
  torch_compile same, but torch.compile(model) (inductor, default mode) whose
                first call includes tracing + inductor/triton compilation

Every measurement is a fresh subprocess, 10 interleaved trials with shuffled
order per (trial, model); medians + paired bootstrap 95% CI vs the ort leg.
This measures deployment cold-start cost of the artifact versus framework
start-up — context only. torch.compile inherits the inductor/triton on-disk
caches (same convention as e8: fresh process, caches untouched); per-trial raw
values are in t12c.csv so a warm-cache trend would be visible. Child failures
are archived verbatim under errors/.

Server usage:
  venv-bench/bin/python benchmarks/e16gpu/t12c_coldstart.py --out ~/camel-eval/e16gpu/t12c
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gpuenv  # noqa: E402

MODELS = ("mlp", "transformer", "resnetbn")
CONFIGS = ("ort", "torch_eager", "torch_compile")


def child_main(args) -> int:
    import time

    try:
        if args.cond == "ort":
            gpuenv.ensure_loader_env(inprocess_ort=True)
            t0 = time.perf_counter()
            import onnx

            import onnxruntime as ort

            t_import = time.perf_counter()
            graph = gpuenv.graph_path(args.model, Path(args.resnetbn_graph) if args.resnetbn_graph else None)
            proto = onnx.load(str(graph))
            t_read = time.perf_counter()
            opts = ort.SessionOptions()
            opts.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
            sess = ort.InferenceSession(proto.SerializeToString(), sess_options=opts, providers=["CUDAExecutionProvider"])
            t_create = time.perf_counter()
            feeds = gpuenv.load_feeds(args.model, graph)
            sess.run(None, feeds)
            t_first = time.perf_counter()
            gpuenv.emit_row(
                status="ok",
                import_s=t_import - t0,
                read_s=t_read - t_import,
                create_s=t_create - t_read,
                first_s=t_first - t_create,
            )
            return 0

        t0 = time.perf_counter()
        import torch

        t_import = time.perf_counter()
        gpuenv.sys_path_bench()
        import train
        from common.weights import load

        weights, x = load(args.model)
        net = train.torch_model(args.model, weights).cuda()
        opt = torch.optim.SGD(net.parameters(), lr=train.LR)
        xt = torch.from_numpy(x).cuda()
        if args.cond == "torch_compile":
            net = torch.compile(net)
        torch.cuda.synchronize()
        t_build = time.perf_counter()
        opt.zero_grad(set_to_none=True)
        train.torch_loss(net, xt).backward()
        opt.step()
        torch.cuda.synchronize()
        t_first = time.perf_counter()
        gpuenv.emit_row(status="ok", import_s=t_import - t0, read_s=0.0, create_s=t_build - t_import, first_s=t_first - t_build)
        return 0
    except Exception as exc:
        import traceback

        traceback.print_exc()
        gpuenv.emit_row(status="failed", error=str(exc)[:400])
        return 1


COLUMNS = ["model", "config", "trial", "status", "wall_s", "import_s", "read_s", "create_s", "first_s", "error"]


def parent_main(args) -> int:
    import csv
    import random

    gpuenv.ensure_loader_env(inprocess_ort=False)
    gpuenv.sys_path_bench()
    from common import stats

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    errors = out / "errors"
    rng = random.Random(args.seed)
    models = [m for m in args.models.split(",") if m]
    configs = [c for c in args.configs.split(",") if c]
    rows = []
    for trial in range(1, args.trials + 1):
        for model in models:
            order = list(configs)
            rng.shuffle(order)
            for config in order:
                cmd = [sys.executable, str(Path(__file__).resolve()), "child", "--model", model, "--cond", config]
                if args.resnetbn_graph:
                    cmd += ["--resnetbn-graph", args.resnetbn_graph]
                t0 = time.perf_counter()
                try:
                    proc = subprocess.run(cmd, capture_output=True, text=True, env=gpuenv.child_env(), timeout=args.timeout)
                    wall = time.perf_counter() - t0
                    row = {"model": model, "config": config, "trial": trial, "wall_s": wall}
                    try:
                        row.update(gpuenv.parse_row(proc.stdout))
                    except Exception:
                        row.update(status="failed", error=f"rc={proc.returncode}: {proc.stderr.strip()[-300:]}")
                    if row.get("status") != "ok":
                        errors.mkdir(parents=True, exist_ok=True)
                        (errors / f"{model}_{config}_t{trial}.log").write_text(
                            f"cmd: {' '.join(cmd)}\nrc={proc.returncode}\n--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}\n"
                        )
                        print(f"t{trial} {model:12s} {config:14s} FAILED (archived): {str(row.get('error'))[:140]}", flush=True)
                    else:
                        print(
                            f"t{trial} {model:12s} {config:14s} wall {wall:7.3f}s "
                            f"(import {row['import_s']:5.2f} read {row['read_s']:5.2f} create {row['create_s']:6.3f} first {row['first_s']:6.3f})",
                            flush=True,
                        )
                except subprocess.TimeoutExpired:
                    row = {"model": model, "config": config, "trial": trial, "wall_s": None, "status": "failed", "error": f"timeout {args.timeout}s"}
                    print(f"t{trial} {model:12s} {config:14s} TIMEOUT", flush=True)
                rows.append({c: row.get(c, "") for c in COLUMNS})

    with (out / "t12c.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)

    summary = []
    for model in models:
        base = [r for r in rows if r["model"] == model and r["config"] == "ort" and r["status"] == "ok"]
        for config in configs:
            cur = [r for r in rows if r["model"] == model and r["config"] == config and r["status"] == "ok"]
            entry = {"model": model, "config": config, "n_ok": len(cur)}
            if cur:
                walls = [float(r["wall_s"]) for r in cur]
                med = stats.median(walls)
                lo, hi = stats.bootstrap_ci(walls, seed=args.seed)
                entry.update(median_wall_s=f"{med:.4f}", ci95=f"[{lo:.4f},{hi:.4f}]",
                             ci_halfwidth_pct=f"{100.0 * (hi - lo) / 2.0 / med:.1f}" if med else "")
                for seg in ("import_s", "read_s", "create_s", "first_s"):
                    entry[f"median_{seg}"] = f"{stats.median([float(r[seg]) for r in cur]):.4f}"
                if config != "ort" and len(base) == len(cur):
                    ratio, rlo, rhi = stats.ratio_ci(walls, [float(r["wall_s"]) for r in base], paired=True, seed=args.seed)
                    entry.update(x_vs_ort=f"{ratio:.2f}", x_ci95=f"[{rlo:.2f},{rhi:.2f}]")
                entry["status"] = "ok" if len(cur) == args.trials else f"partial({len(cur)}/{args.trials})"
            else:
                entry["status"] = "failed"
            summary.append(entry)
            print(f"  {model:12s} {config:14s} {entry.get('median_wall_s','-'):>8}s wall  {entry.get('ci95','')}  {entry['status']}")

    scols = ["model", "config", "status", "n_ok", "median_wall_s", "ci95", "ci_halfwidth_pct",
             "median_import_s", "median_read_s", "median_create_s", "median_first_s", "x_vs_ort", "x_ci95"]
    with (out / "t12c_summary.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=scols, extrasaction="ignore")
        w.writeheader()
        w.writerows(summary)
    gpuenv.write_meta(out, {
        "subitem": "T12c",
        "framing": "cold start of ORT CUDA EP executing our exported graph vs PyTorch framework (eager / "
                   "torch.compile default) first training step; deployment overhead, context only",
        "models": models, "configs": configs, "trials": args.trials,
        "cache_note": "fresh subprocess per measurement; inductor/triton on-disk caches left untouched (e8 convention)",
        "graphs": {m: str(gpuenv.graph_path(m, Path(args.resnetbn_graph) if args.resnetbn_graph else None)) for m in models},
    })
    print(f"[done] wrote {out}/t12c.csv, t12c_summary.csv, meta.json")
    return 0 if all(e["status"] == "ok" for e in summary) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode")
    c = sub.add_parser("child")
    c.add_argument("--model", required=True)
    c.add_argument("--cond", required=True, choices=CONFIGS)
    c.add_argument("--resnetbn-graph", default="")
    p = sub.add_parser("run")
    p.add_argument("--models", default=",".join(MODELS))
    p.add_argument("--configs", default=",".join(CONFIGS))
    p.add_argument("--trials", type=int, default=10)
    p.add_argument("--timeout", type=float, default=900.0)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--resnetbn-graph", default="")
    p.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if args.mode == "child":
        return child_main(args)
    if args.mode == "run":
        return parent_main(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
