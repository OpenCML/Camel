"""T12b batch-scaling training-step throughput curves (MLSys 2027 eval; context only).

For resnetbn (literal ResNet-18 + BN, T4 graph family) and transformer_big
(E5-W1 scale), the training-step graph is re-exported per batch size with the
server's Linux camel (train_export workload, input baked to [B, ...]), then:

  ort    one unmodified ORT InferenceSession (CUDA EP, CPU fallback disabled)
         executes the exported step graph with static feeds (E3 timing pattern:
         the graph's own SGD update runs; outputs are discarded, not fed back)
  torch  PyTorch eager trains the same model (mean(y*y) loss, SGD lr 0.01,
         weights resident on the GPU) — the framework-side reference

Throughput (steps/s) is deployment context for the artifact, never a kernel
performance claim. Where cuDNN wins (the conv stack), that is the runtime's
kernel, not ours; where the recursive/explicit formulations lose to fused
library kernels, that is attributed to the library, not the framework.

Batches: 1, 8, 32, 64, 128, 256, 512. Per (model, batch) the two conditions are
timed in interleaved trials (shuffled order), medians + bootstrap 95% CI.
OOM at a batch size is data: the child's stderr is archived verbatim, the row
is marked oom, and larger batches are skipped for that (model, condition).

Phases:
  export   generate per-batch inputs and re-export both models' step graphs
  run      measure throughput (writes t12b.csv / t12b_summary.csv / meta.json)
  plot     log-log steps/s vs batch png (t12b.png)

Server usage:
  venv-bench/bin/python benchmarks/e16gpu/t12b_throughput.py export --out ~/camel-eval/e16gpu/t12b
  venv-bench/bin/python benchmarks/e16gpu/t12b_throughput.py run    --out ~/camel-eval/e16gpu/t12b
  venv-bench/bin/python benchmarks/e16gpu/t12b_throughput.py plot   --out ~/camel-eval/e16gpu/t12b
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gpuenv  # noqa: E402

MODELS = ("resnetbn", "transformer_big")
BATCHES = (1, 8, 32, 64, 128, 256, 512)
CONDS = ("ort", "torch_eager")
INPUT_SHAPE = {"resnetbn": (3, 32, 32), "transformer_big": (64, 256)}
WARMUP = 5


def graph_for(out: Path, model: str, batch: int) -> Path:
    return out / "graphs" / f"{model}_b{batch}.onnx"


def input_for(out: Path, model: str, batch: int) -> Path:
    return out / "inputs" / f"{model}_b{batch}.npy"


# ---------------------------------------------------------------------------
# export phase
# ---------------------------------------------------------------------------


def export_phase(out: Path, models: list[str], batches: list[int], timeout: float) -> None:
    import numpy as np
    import onnx

    (out / "inputs").mkdir(parents=True, exist_ok=True)
    (out / "graphs").mkdir(parents=True, exist_ok=True)
    (out / "errors").mkdir(parents=True, exist_ok=True)
    gpuenv.sys_path_bench()
    from common.weights import load  # noqa: F401  (ensures artifacts exist)

    for model in models:
        load(model)
        rng = np.random.default_rng(abs(hash(model)) % 2**31)
        for b in batches:
            x = rng.standard_normal((b, *INPUT_SHAPE[model])).astype(np.float32)
            np.save(input_for(out, model, b), x)
    for model in models:
        for b in batches:
            graph = graph_for(out, model, b)
            if graph.exists():
                continue
            env = gpuenv.child_env()
            env.update(
                {
                    "CAMEL_HOME": str(gpuenv.REPO / "out" / "latest"),
                    "CAMEL_PACKAGES": str(gpuenv.BENCH / "common"),
                    "CAMEL_BENCH_WEIGHTS": str(gpuenv.BENCH / "artifacts" / model / "weights"),
                    "CAMEL_BENCH_INPUT": str(input_for(out, model, b)),
                    "CAMEL_BENCH_WORKLOAD": "train_export",
                    "CAMEL_BENCH_EXPORT": str(graph),
                }
            )
            cml = gpuenv.BENCH / model / "model.cml"
            cmd = [str(gpuenv.CAMEL), str(cml)]
            t0 = time.perf_counter()
            proc = subprocess.run(cmd, cwd=cml.parent, env=env, capture_output=True, text=True, timeout=timeout)
            wall = time.perf_counter() - t0
            if proc.returncode != 0 or not graph.exists():
                (out / "errors" / f"export_{model}_b{b}.log").write_text(
                    f"cmd: {' '.join(cmd)}\nrc={proc.returncode}\n--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}\n"
                )
                print(f"[export] {model} b{b}: FAILED ({wall:.1f}s, archived)", flush=True)
                continue
            proto = onnx.load(str(graph))
            onnx.checker.check_model(proto, full_check=True)
            print(f"[export] {model} b{b}: {len(proto.graph.node)} nodes, checker ok ({wall:.1f}s)", flush=True)


# ---------------------------------------------------------------------------
# children
# ---------------------------------------------------------------------------


def child_ort(model: str, batch: int, out: Path, target_s: float) -> dict:
    import numpy as np

    import onnxruntime as ort

    graph = graph_for(out, model, batch)
    gpuenv.sys_path_bench()
    from common.weights import load

    weights, _ = load(model)
    feeds = {**weights, "input1": np.load(input_for(out, model, batch))}
    opts = ort.SessionOptions()
    opts.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
    sess = ort.InferenceSession(str(graph), sess_options=opts, providers=["CUDAExecutionProvider"])
    for _ in range(WARMUP):
        sess.run(None, feeds)
    w0 = time.perf_counter()
    sess.run(None, feeds)
    probe = time.perf_counter() - w0
    n = max(10, min(300, int(round(target_s / max(probe, 1e-5)))))
    times = []
    for _ in range(n):
        t0 = time.perf_counter()
        sess.run(None, feeds)
        times.append(time.perf_counter() - t0)
    times.sort()
    return {"n_steps": n, "step_ms_median": times[n // 2] * 1000.0, "step_ms_min": times[0] * 1000.0}


def child_torch(model: str, batch: int, out: Path, target_s: float) -> dict:
    import numpy as np
    import torch

    gpuenv.sys_path_bench()
    import train
    from common.weights import load

    weights, _ = load(model)
    net = train.torch_model(model, weights).cuda()
    opt = torch.optim.SGD(net.parameters(), lr=train.LR)
    xt = torch.from_numpy(np.load(input_for(out, model, batch))).cuda()

    def step():
        opt.zero_grad(set_to_none=True)
        train.torch_loss(net, xt).backward()
        opt.step()

    for _ in range(WARMUP):
        step()
    torch.cuda.synchronize()
    w0 = time.perf_counter()
    step()
    torch.cuda.synchronize()
    probe = time.perf_counter() - w0
    n = max(10, min(300, int(round(target_s / max(probe, 1e-5)))))
    times = []
    for _ in range(n):
        t0 = time.perf_counter()
        step()
        torch.cuda.synchronize()
        times.append(time.perf_counter() - t0)
    times.sort()
    return {"n_steps": n, "step_ms_median": times[n // 2] * 1000.0, "step_ms_min": times[0] * 1000.0}


def child_main(args) -> int:
    gpuenv.ensure_loader_env(inprocess_ort=args.cond == "ort")
    try:
        if args.cond == "ort":
            res = child_ort(args.model, args.batch, Path(args.out), args.target_s)
        else:
            res = child_torch(args.model, args.batch, Path(args.out), args.target_s)
        gpuenv.emit_row(status="ok", **res)
        return 0
    except Exception as exc:
        import traceback

        traceback.print_exc()
        msg = str(exc)
        gpuenv.emit_row(status="oom" if "out of memory" in msg.lower() else "failed", error=msg[:400])
        return 1


# ---------------------------------------------------------------------------
# run phase (parent)
# ---------------------------------------------------------------------------

COLUMNS = ["model", "batch", "cond", "trial", "status", "n_steps", "step_ms_median", "step_ms_min", "steps_per_s", "error"]


def run_one(args, model: str, batch: int, cond: str, trial: int, errors: Path) -> dict:
    cmd = [
        sys.executable, str(Path(__file__).resolve()), "child",
        "--model", model, "--batch", str(batch), "--cond", cond,
        "--target-s", str(args.target_s), "--out", str(args.out),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=gpuenv.child_env(), timeout=args.timeout)
    row = {"model": model, "batch": batch, "cond": cond, "trial": trial}
    try:
        row.update(gpuenv.parse_row(proc.stdout))
    except Exception:
        row.update(status="failed", error=f"rc={proc.returncode}: {proc.stderr.strip()[-300:]}")
    if row.get("status") != "ok":
        errors.mkdir(parents=True, exist_ok=True)
        (errors / f"{model}_b{batch}_{cond}_t{trial}.log").write_text(
            f"cmd: {' '.join(cmd)}\nrc={proc.returncode}\n--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}\n"
        )
        if "out of memory" in (proc.stderr or "").lower() or "out of memory" in str(row.get("error", "")).lower():
            row["status"] = "oom"
    if row.get("status") == "ok":
        row["steps_per_s"] = 1000.0 / float(row["step_ms_median"])
    return row


def run_phase(args) -> int:
    import csv
    import random

    gpuenv.ensure_loader_env(inprocess_ort=False)
    gpuenv.sys_path_bench()
    from common import stats

    out = Path(args.out)
    errors = out / "errors"
    rng = random.Random(args.seed)
    models = [m for m in args.models.split(",") if m]
    batches = [int(b) for b in args.batches.split(",") if b]
    rows = []
    oom_seen: set[tuple[str, str]] = set()
    for trial in range(1, args.trials + 1):
        for model in models:
            for batch in batches:
                order = list(CONDS)
                rng.shuffle(order)
                for cond in order:
                    if (model, cond) in oom_seen:
                        continue
                    row = run_one(args, model, batch, cond, trial, errors)
                    rows.append({c: row.get(c, "") for c in COLUMNS})
                    if row.get("status") == "oom":
                        oom_seen.add((model, cond))
                        print(f"t{trial} {model:16s} b{batch:<4d} {cond:12s} OOM (archived)", flush=True)
                    elif row.get("status") != "ok":
                        print(f"t{trial} {model:16s} b{batch:<4d} {cond:12s} FAILED (archived): {str(row.get('error'))[:120]}", flush=True)
                    else:
                        print(f"t{trial} {model:16s} b{batch:<4d} {cond:12s} {row['step_ms_median']:9.3f} ms  {row['steps_per_s']:9.1f} steps/s", flush=True)

    with (out / "t12b.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)

    summary = []
    for model in models:
        for batch in batches:
            for cond in CONDS:
                cur = [r for r in rows if r["model"] == model and r["batch"] == batch and r["cond"] == cond and r["status"] == "ok"]
                failed = [r for r in rows if r["model"] == model and r["batch"] == batch and r["cond"] == cond and r["status"] != "ok"]
                entry = {"model": model, "batch": batch, "cond": cond}
                if cur:
                    vals = [float(r["steps_per_s"]) for r in cur]
                    med = stats.median(vals)
                    lo, hi = stats.bootstrap_ci(vals, seed=args.seed)
                    entry.update(steps_per_s=f"{med:.2f}", ci95=f"[{lo:.2f},{hi:.2f}]",
                                 ci_halfwidth_pct=f"{100.0 * (hi - lo) / 2.0 / med:.1f}" if med else "", n_ok=len(cur),
                                 status="ok" if len(cur) == args.trials else f"partial({len(cur)}/{args.trials})")
                else:
                    entry.update(steps_per_s="", ci95="", ci_halfwidth_pct="", n_ok=0,
                                 status=failed[0]["status"] if failed else "skipped")
                summary.append(entry)
    with (out / "t12b_summary.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["model", "batch", "cond", "status", "n_ok", "steps_per_s", "ci95", "ci_halfwidth_pct"])
        w.writeheader()
        w.writerows(summary)
    gpuenv.write_meta(out, {
        "subitem": "T12b",
        "framing": "ORT CUDA EP executing our re-exported per-batch training-step graphs vs PyTorch eager "
                   "framework training; deployment context only, not a kernel-performance contest",
        "models": models, "batches": batches, "trials": args.trials, "warmup": WARMUP,
        "ort_feeds": "static (E3 timing pattern): weights+input bound once per step, outputs discarded",
        "oom_points": sorted(f"{m}/{c}" for (m, c) in oom_seen),
    })
    for e in summary:
        print(f"  {e['model']:16s} b{e['batch']:<4d} {e['cond']:12s} {e['status']:8s} {e['steps_per_s'] or '-':>10} steps/s {e['ci95']}")
    print(f"[done] wrote {out}/t12b.csv, t12b_summary.csv, meta.json")
    return 0


# ---------------------------------------------------------------------------
# plot phase
# ---------------------------------------------------------------------------


def plot_phase(args) -> int:
    import csv

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    out = Path(args.out)
    with (out / "t12b_summary.csv").open() as f:
        rows = list(csv.DictReader(f))
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
    for ax, model in zip(axes, [m for m in args.models.split(",") if m]):
        for cond, marker in (("ort", "o"), ("torch_eager", "s")):
            pts = [(int(r["batch"]), float(r["steps_per_s"])) for r in rows
                   if r["model"] == model and r["cond"] == cond and r["steps_per_s"]]
            if not pts:
                continue
            pts.sort()
            ax.plot([p[0] for p in pts], [p[1] for p in pts], marker=marker,
                    label="ORT CUDA EP (our exported graph)" if cond == "ort" else "PyTorch eager (framework)")
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xticks([1, 8, 32, 64, 128, 256, 512])
        ax.set_xticklabels([1, 8, 32, 64, 128, 256, 512])
        ax.set_xlabel("batch size")
        ax.set_ylabel("training steps / s")
        ax.set_title(f"{model} (context only)")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend(fontsize=8)
    fig.suptitle("T12b batch-scaling throughput — artifact deployment vs framework training (context only)")
    fig.tight_layout()
    png = out / "t12b.png"
    fig.savefig(png, dpi=150)
    print(f"[done] wrote {png}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="phase")
    for name in ("export", "run", "plot"):
        p = sub.add_parser(name)
        p.add_argument("--models", default=",".join(MODELS))
        p.add_argument("--out", type=Path, required=True)
        if name == "export":
            p.add_argument("--batches", default=",".join(str(b) for b in BATCHES))
            p.add_argument("--timeout", type=float, default=600.0)
        if name == "run":
            p.add_argument("--batches", default=",".join(str(b) for b in BATCHES))
            p.add_argument("--trials", type=int, default=3)
            p.add_argument("--target-s", type=float, default=2.0)
            p.add_argument("--timeout", type=float, default=1200.0)
            p.add_argument("--seed", type=int, default=0)
    c = sub.add_parser("child")
    c.add_argument("--model", required=True)
    c.add_argument("--batch", type=int, required=True)
    c.add_argument("--cond", required=True, choices=CONDS)
    c.add_argument("--target-s", type=float, default=2.0)
    c.add_argument("--out", required=True)
    args = ap.parse_args()
    if args.phase == "export":
        gpuenv.ensure_loader_env(inprocess_ort=False)
        export_phase(args.out, [m for m in args.models.split(",") if m], [int(b) for b in args.batches.split(",") if b], args.timeout)
        return 0
    if args.phase == "run":
        return run_phase(args)
    if args.phase == "plot":
        return plot_phase(args)
    if args.phase == "child":
        return child_main(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
