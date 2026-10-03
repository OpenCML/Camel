"""T12a GPU memory footprint contrast (MLSys 2027 eval, A.4/A.5 discipline).

Question: for one deployed training step, what GPU memory does the *artifact*
cost versus the *framework*? For each model (mlp, gru, transformer from the E3
bundle; resnetbn from T4) and each condition:

  ort           an unmodified ORT InferenceSession (CUDA EP, CPU fallback
                disabled) executing our exported training-step graph in a
                feed-back loop (updated weights are fed back, as deployed)
  torch_eager   PyTorch eager training the same model (train.py reference,
                mean(y*y) loss, SGD lr 0.01) on the same GPU
  torch_compile same training step under torch.compile (inductor, default mode)

Per measurement a fresh child process runs; the parent samples the child's
per-process GPU memory via nvidia-smi compute-apps. Reported per (model,
condition): context+model resident before the first step (settle window),
peak during the training loop, resident after the loop with the model/session
still alive, plus torch.cuda.max_memory_allocated for the torch legs. These
are deployment-overhead numbers, context only — not kernel-performance claims.

Trials are interleaved per model with shuffled condition order; medians and
bootstrap 95% CIs go to t12a_summary.csv. Raw nvidia-smi series per child are
kept under raw/; child failures are archived verbatim under errors/.

Server usage:
  venv-bench/bin/python benchmarks/e16gpu/t12a_memory.py --out ~/camel-eval/e16gpu/t12a
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

MODELS = ("mlp", "gru", "transformer", "resnetbn")
CONDS = ("ort", "torch_eager", "torch_compile")
SETTLE_S = 1.0
RESIDENT_S = 2.0


# ---------------------------------------------------------------------------
# child: one (model, condition) measurement in a fresh process
# ---------------------------------------------------------------------------


def _adaptive_n(avg_step_s: float, target_s: float) -> int:
    return max(30, min(6000, int(round(target_s / max(avg_step_s, 1e-5)))))


def child_ort(model: str, graph: Path, target_s: float) -> dict:
    t0 = time.perf_counter()
    import onnxruntime as ort

    t_import = time.perf_counter()
    feeds = gpuenv.load_feeds(model, graph)
    opts = ort.SessionOptions()
    opts.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
    cuda_mode = "strict"
    try:
        sess = ort.InferenceSession(str(graph), sess_options=opts, providers=["CUDAExecutionProvider"])
    except Exception as exc:
        print(f"[child] strict CUDA session failed ({str(exc)[:160]}); retrying with CPU fallback allowed", flush=True)
        cuda_mode = "partial"
        sess = ort.InferenceSession(str(graph), providers=["CUDAExecutionProvider"])
    t_ctx = time.perf_counter()
    time.sleep(SETTLE_S)

    ins = [i.name for i in sess.get_inputs()]
    outs = [o.name for o in sess.get_outputs()]
    in_to_out = {i: o for o, i in gpuenv.feedback_map(sess).items()}

    def step(f):
        upd = dict(zip(outs, sess.run(None, f)))
        return {n: (upd[in_to_out[n]] if n in in_to_out else f[n]) for n in ins}

    t_warm = time.perf_counter()
    w0 = time.perf_counter()
    f = step(feeds)
    for _ in range(2):
        f = step(f)
    avg = (time.perf_counter() - w0) / 3.0
    n = _adaptive_n(avg, target_s)

    t_loop = time.perf_counter()
    for _ in range(n):
        f = step(f)
    t_end = time.perf_counter()
    time.sleep(RESIDENT_S)
    return {
        "n_steps": n,
        "cuda_mode": cuda_mode,
        "t_import": t_import - t0,
        "t_ctx": t_ctx,
        "t_warm": t_warm,
        "t_loop": t_loop,
        "t_end": t_end,
    }


def child_torch(model: str, cond: str, target_s: float) -> dict:
    t0 = time.perf_counter()
    import torch

    t_import = time.perf_counter()
    gpuenv.sys_path_bench()
    import train
    from common.weights import load

    weights, x = load(model)
    net = train.torch_model(model, weights).cuda()
    opt = torch.optim.SGD(net.parameters(), lr=train.LR)
    xt = torch.from_numpy(x).cuda()
    if cond == "torch_compile":
        net = torch.compile(net)

    def step():
        opt.zero_grad(set_to_none=True)
        train.torch_loss(net, xt).backward()
        opt.step()

    torch.cuda.synchronize()
    t_ctx = time.perf_counter()
    time.sleep(SETTLE_S)

    t_warm = time.perf_counter()
    step()  # for torch_compile this first call includes inductor compilation
    torch.cuda.synchronize()
    w0 = time.perf_counter()
    for _ in range(2):
        step()
    torch.cuda.synchronize()
    avg = (time.perf_counter() - w0) / 2.0
    n = _adaptive_n(avg, target_s)

    t_loop = time.perf_counter()
    for _ in range(n):
        step()
    torch.cuda.synchronize()
    t_end = time.perf_counter()
    max_alloc = torch.cuda.max_memory_allocated() / 2**20
    max_reserved = torch.cuda.max_memory_reserved() / 2**20
    time.sleep(RESIDENT_S)
    return {
        "n_steps": n,
        "t_import": t_import - t0,
        "t_ctx": t_ctx,
        "t_warm": t_warm,
        "t_loop": t_loop,
        "t_end": t_end,
        "torch_max_alloc_mb": max_alloc,
        "torch_max_reserved_mb": max_reserved,
    }


def child_main(args) -> int:
    gpuenv.ensure_loader_env(inprocess_ort=args.cond == "ort")
    graph = gpuenv.graph_path(args.model, Path(args.resnetbn_graph) if args.resnetbn_graph else None)
    try:
        if args.cond == "ort":
            res = child_ort(args.model, graph, args.target_s)
        else:
            res = child_torch(args.model, args.cond, args.target_s)
        gpuenv.emit_row(status="ok", **res)
        return 0
    except Exception as exc:
        import traceback

        traceback.print_exc()
        gpuenv.emit_row(status="failed", error=str(exc)[:400])
        return 1


# ---------------------------------------------------------------------------
# parent: interleaved trials + nvidia-smi sampling
# ---------------------------------------------------------------------------


def run_one(args, model: str, cond: str, trial: int, errors: Path) -> dict:
    cmd = [
        sys.executable,
        str(Path(__file__).resolve()),
        "child",
        "--model",
        model,
        "--cond",
        cond,
        "--target-s",
        str(args.target_s),
    ]
    if args.resnetbn_graph:
        cmd += ["--resnetbn-graph", args.resnetbn_graph]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=gpuenv.child_env())
    row = {"model": model, "cond": cond, "trial": trial, "status": "spawn_failed"}
    with gpuenv.SmiSampler([proc.pid], interval=args.sample_interval) as sampler:
        stdout, stderr = proc.communicate(timeout=args.timeout)
    try:
        res = gpuenv.parse_row(stdout)
        row.update(res)
    except Exception:
        row.update(status="failed", error=f"rc={proc.returncode}: {stderr.strip()[-300:]}")
    if proc.returncode != 0 or row.get("status") != "ok":
        errors.mkdir(parents=True, exist_ok=True)
        (errors / f"{model}_{cond}_t{trial}.log").write_text(
            f"cmd: {' '.join(cmd)}\nrc={proc.returncode}\n--- stdout ---\n{stdout}\n--- stderr ---\n{stderr}\n"
        )
        row["status"] = "failed"
    # join sampler samples with the child's phase timestamps (same CLOCK_MONOTONIC domain)
    series = sampler.series(proc.pid)
    if row.get("status") == "ok" and series:
        t_ctx, t_warm = row["t_ctx"], row["t_warm"]
        t_loop, t_end = row["t_loop"], row["t_end"]
        ctx = gpuenv.window_stats(series, t_ctx, t_warm)
        loop = gpuenv.window_stats(series, t_warm, t_end)
        resident = gpuenv.window_stats(series, t_end + 0.5, t_end + RESIDENT_S + 1.0)
        row.update(
            ctx_mb=ctx["median"],
            ctx_samples=ctx["n"],
            peak_mb=loop["peak"],
            loop_samples=loop["n"],
            resident_mb=resident["median"],
            resident_samples=resident["n"],
        )
        if args.raw_dir:
            raw = Path(args.raw_dir)
            raw.mkdir(parents=True, exist_ok=True)
            with (raw / f"{model}_{cond}_t{trial}.jsonl").open("w") as f:
                for t, mb in series:
                    f.write(f"{t:.4f} {mb:.0f}\n")
    return row


COLUMNS = [
    "model", "cond", "trial", "status", "n_steps", "cuda_mode",
    "ctx_mb", "peak_mb", "resident_mb", "torch_max_alloc_mb", "torch_max_reserved_mb",
    "ctx_samples", "loop_samples", "resident_samples", "error",
]


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
    conds = [c for c in args.conds.split(",") if c]
    rows = []
    for trial in range(1, args.trials + 1):
        for model in models:
            order = conds[:]
            rng.shuffle(order)
            for cond in order:
                row = run_one(args, model, cond, trial, errors)
                rows.append({c: row.get(c, "") for c in COLUMNS})
                print(
                    f"t{trial} {model:12s} {cond:14s} {row.get('status','?'):7s} "
                    f"ctx {row.get('ctx_mb') or 0:.0f}  peak {row.get('peak_mb') or 0:.0f}  "
                    f"resident {row.get('resident_mb') or 0:.0f} MiB"
                    + (f"  (torch alloc {row.get('torch_max_alloc_mb') or 0:.0f})" if cond != "ort" else ""),
                    flush=True,
                )

    raw_csv = out / "t12a.csv"
    with raw_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)

    summary = []
    for model in models:
        for cond in conds:
            cur = [r for r in rows if r["model"] == model and r["cond"] == cond and r["status"] == "ok"]
            entry = {"model": model, "cond": cond, "n_ok": len(cur)}
            for key in ("ctx_mb", "peak_mb", "resident_mb", "torch_max_alloc_mb"):
                vals = [float(r[key]) for r in cur if r.get(key) not in ("", None)]
                if vals:
                    med = stats.median(vals)
                    lo, hi = stats.bootstrap_ci(vals, seed=args.seed)
                    half = (hi - lo) / 2.0
                    entry[key] = f"{med:.0f}"
                    entry[f"{key}_ci95"] = f"[{lo:.0f},{hi:.0f}]"
                    entry[f"{key}_ci_halfwidth_pct"] = f"{100.0 * half / med:.1f}" if med else ""
                else:
                    entry[key] = ""
            entry["status"] = "ok" if len(cur) == args.trials else ("failed" if not cur else f"partial({len(cur)}/{args.trials})")
            summary.append(entry)
            print(
                f"  {model:12s} {cond:14s} ctx {entry.get('ctx_mb') or '-':>6}  peak {entry.get('peak_mb') or '-':>6} "
                f"resident {entry.get('resident_mb') or '-':>6} MiB  {entry['status']}",
                flush=True,
            )

    scols = ["model", "cond", "n_ok", "status"]
    for key in ("ctx_mb", "peak_mb", "resident_mb", "torch_max_alloc_mb"):
        scols += [key, f"{key}_ci95", f"{key}_ci_halfwidth_pct"]
    with (out / "t12a_summary.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=scols, extrasaction="ignore")
        w.writeheader()
        w.writerows(summary)
    gpuenv.write_meta(out, {
        "subitem": "T12a",
        "framing": "ORT executing our exported graph vs PyTorch framework training the same model; "
                   "deployment overhead / framework resident cost, context only — not a kernel-performance contest",
        "models": models, "conds": conds, "trials": args.trials,
        "settle_s": SETTLE_S, "resident_s": RESIDENT_S, "target_loop_s": args.target_s,
        "sample_interval_s": args.sample_interval,
        "graphs": {m: str(gpuenv.graph_path(m, Path(args.resnetbn_graph) if args.resnetbn_graph else None)) for m in models},
    })
    print(f"[done] wrote {raw_csv} and summary/meta under {out}")
    return 0 if all(e["status"] == "ok" for e in summary) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode")
    c = sub.add_parser("child")
    c.add_argument("--model", required=True)
    c.add_argument("--cond", required=True, choices=CONDS)
    c.add_argument("--target-s", type=float, default=2.5)
    c.add_argument("--resnetbn-graph", default="")
    p = sub.add_parser("run")
    p.add_argument("--models", default=",".join(MODELS))
    p.add_argument("--conds", default=",".join(CONDS))
    p.add_argument("--trials", type=int, default=3)
    p.add_argument("--target-s", type=float, default=2.5, help="approximate seconds of training loop per child")
    p.add_argument("--sample-interval", type=float, default=0.02)
    p.add_argument("--timeout", type=float, default=900.0)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--resnetbn-graph", default="")
    p.add_argument("--raw-dir", default="")
    p.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if args.mode == "child":
        return child_main(args)
    if args.mode == "run":
        if not args.raw_dir:
            args.raw_dir = str(Path(args.out) / "raw")
        return parent_main(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
