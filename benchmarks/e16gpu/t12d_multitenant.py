# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T12d single-GPU multi-tenant LoRA fine-tuning (MLSys 2027 eval).

One exported LoRA training-step graph (E9's lora_ce_step_lr01.onnx, backbone
frozen, adapters trainable), N independent tenants on ONE GPU, each tenant
running 50 fine-tune steps on the rotated-MNIST user batches:

  ort    N unmodified ORT InferenceSessions in ONE process (one CUDA context
         via the runtime API), one python thread per session (session.run
         releases the GIL); each session owns an independent copy of the
         adapter/state weights, initialized identically
  torch  N independent OS processes, each a PyTorch eager LoRA training loop
         on the same GPU (one CUDA context per process — the framework's
         per-tenant resident cost)

Measured per N in {1,2,4,8,16,32}: aggregate throughput (steps/s), peak GPU
memory, and final-state consistency — sampled sessions' final weights and
eval accuracy must match the single-tenant baseline (same init, same batch
order). CUDA OOM is data: the first N where a torch child (or an ORT session)
fails with out-of-memory is recorded with stderr archived verbatim, and that
leg stops growing.

Deployment-framing: per-tenant marginal cost of serving one more fine-tune
from the same artifact versus one more framework process. Throughput is
context only.

Server usage:
  venv-bench/bin/python benchmarks/e16gpu/t12d_multitenant.py run --out ~/camel-eval/e16gpu/t12d
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gpuenv  # noqa: E402

NS = (1, 2, 4, 8, 16, 32)
STEPS = 50
NB = 32
LORA_ART = gpuenv.BENCH / "artifacts" / "lora"
LORA_GRAPH = gpuenv.BENCH / "lora" / "out" / "lora_ce_step_lr01.onnx"
PARAM_NAMES = [
    "fc1_w", "fc1_b", "fc1_lora_a", "fc1_lora_b",
    "fc2_w", "fc2_b", "fc2_lora_a", "fc2_lora_b",
    "fc3_w", "fc3_b", "fc3_lora_a", "fc3_lora_b",
]


def load_init():
    import numpy as np

    return {n: np.load(LORA_ART / "pretrained" / f"{n}.npy") for n in PARAM_NAMES}


def load_batch(i: int):
    import numpy as np

    d = LORA_ART / "user_batches"
    return np.load(d / f"batch_{i % NB}.npy"), np.load(d / f"target_{i % NB}.npy")


def accuracy(weights: dict) -> float:
    """Shared eval: LoRA forward on the rotated eval set (torch, CPU only)."""
    import numpy as np
    import torch

    x = torch.from_numpy(np.load(LORA_ART / "user_eval.npy"))
    y = np.load(LORA_ART / "user_eval_target.npy")
    h = x
    for n in ("fc1", "fc2", "fc3"):
        h = h @ torch.from_numpy(weights[f"{n}_w"]) + (h @ torch.from_numpy(weights[f"{n}_lora_b"])) @ torch.from_numpy(
            weights[f"{n}_lora_a"]
        ) * 2.0 + torch.from_numpy(weights[f"{n}_b"])
        if n != "fc3":
            h = torch.relu(h)
    return float((h.argmax(1).numpy() == y).mean())


# ---------------------------------------------------------------------------
# children
# ---------------------------------------------------------------------------


def ort_child(args) -> int:
    """N ORT sessions (one process, one thread per session), 50 steps each."""
    import numpy as np

    gpuenv.ensure_loader_env(inprocess_ort=True)
    import onnxruntime as ort

    import os

    pid = os.getpid()
    sampler = gpuenv.SmiSampler([pid], interval=0.02)
    sampler.__enter__()
    t0 = time.perf_counter()
    init = load_init()
    batches = [load_batch(i) for i in range(NB)]

    opts = ort.SessionOptions()
    opts.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
    opts.intra_op_num_threads = 1
    sessions = []
    t_create0 = time.perf_counter()
    try:
        for _ in range(args.n):
            sessions.append(ort.InferenceSession(str(LORA_GRAPH), sess_options=opts, providers=["CUDAExecutionProvider"]))
    except Exception as exc:
        sampler.__exit__(None, None, None)
        gpuenv.emit_row(status="oom" if "out of memory" in str(exc).lower() else "failed", error=f"session create: {str(exc)[:300]}")
        return 1
    t_created = time.perf_counter()

    ins = [i.name for i in sessions[0].get_inputs()]
    outs = [o.name for o in sessions[0].get_outputs()]

    results: dict[int, dict] = {}
    errors: list[str] = []

    def tenant(s: int) -> None:
        try:
            sess = sessions[s]
            w = {n: init[n].copy() for n in PARAM_NAMES}
            for i in range(args.steps):
                x, t = batches[i % NB]
                feeds = {name: w[name] for name in ins if name not in ("input1", "input2")}
                feeds["input1"], feeds["input2"] = x, t
                res = dict(zip(outs, sess.run(None, feeds)))
                w = {n: res[f"output_1_{n}"] for n in PARAM_NAMES}
            results[s] = w
        except Exception as exc:  # noqa: BLE001
            errors.append(f"tenant {s}: {str(exc)[:300]}")

    t_run0 = time.perf_counter()
    threads = [threading.Thread(target=tenant, args=(s,)) for s in range(args.n)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    t_run1 = time.perf_counter()
    time.sleep(1.5)  # resident window for the sampler
    t_end = time.perf_counter()
    sampler.__exit__(None, None, None)

    if errors:
        msg = "; ".join(errors)
        gpuenv.emit_row(status="oom" if "out of memory" in msg.lower() else "failed", error=msg[:400])
        return 1

    series = sampler.series(pid)
    create_w = gpuenv.window_stats(series, t_created - 0.2, t_run0)
    run_w = gpuenv.window_stats(series, t_run0, t_run1)
    res_w = gpuenv.window_stats(series, t_run1 + 0.5, t_end)

    sampled = sorted({0, args.n // 2, args.n - 1})
    pair_diff = 0.0
    for a, b in zip(sampled, sampled[1:]):
        for n in PARAM_NAMES:
            pair_diff = max(pair_diff, float(np.max(np.abs(results[a][n] - results[b][n]))))
    base_diff = None
    if args.baseline and Path(args.baseline).exists():
        base = np.load(args.baseline)
        base_diff = max(float(np.max(np.abs(results[0][n] - base[n]))) for n in PARAM_NAMES)
    if args.dump:
        np.savez(args.dump, **results[0])

    # accuracy eval last: torch import must not pollute the memory windows above
    accs = {}
    for s in sampled:
        accs[f"acc_s{s}"] = accuracy(results[s])

    gpuenv.emit_row(
        status="ok",
        wall_s=t_run1 - t_run0,
        create_s=t_created - t_create0,
        aggregate_steps_per_s=args.n * args.steps / (t_run1 - t_run0),
        created_mb=create_w["median"],
        peak_mb=run_w["peak"],
        resident_mb=res_w["median"],
        per_tenant_peak_mb=(run_w["peak"] / args.n) if run_w["peak"] else None,
        pairwise_maxdiff=pair_diff,
        baseline_maxdiff=base_diff,
        **accs,
    )
    return 0


def torch_child(args) -> int:
    """One PyTorch eager LoRA tenant on the GPU; trains 50 steps after a start barrier."""
    import numpy as np
    import torch

    init = load_init()
    adapters = {n: torch.from_numpy(init[n]).cuda().requires_grad_(True) for n in PARAM_NAMES if "lora" in n}
    frozen = {n: torch.from_numpy(init[n]).cuda() for n in PARAM_NAMES if "lora" not in n}
    batches = [load_batch(i) for i in range(NB)]
    print("READY", flush=True)  # import + CUDA context + model done; wait for the GO barrier
    sys.stdin.readline()
    t0 = time.perf_counter()
    for i in range(args.steps):
        x, t = batches[i % NB]
        xt, tt = torch.from_numpy(x).cuda(), torch.from_numpy(t).cuda()
        h = xt
        for n in ("fc1", "fc2", "fc3"):
            h = h @ frozen[f"{n}_w"] + (h @ adapters[f"{n}_lora_b"]) @ adapters[f"{n}_lora_a"] * 2.0 + frozen[f"{n}_b"]
            if n != "fc3":
                h = torch.relu(h)
        loss = torch.nn.functional.cross_entropy(h, tt.argmax(1))
        loss.backward()
        with torch.no_grad():
            for p in adapters.values():
                p -= 0.1 * p.grad
                p.grad = None
    torch.cuda.synchronize()
    wall = time.perf_counter() - t0
    if args.dump:
        final = {**{n: frozen[n].cpu().numpy() for n in frozen}, **{n: adapters[n].detach().cpu().numpy() for n in adapters}}
        np.savez(args.dump, **final)
    gpuenv.emit_row(status="ok", wall_s=wall, final_loss=float(loss))
    return 0


def eval_child(args) -> int:
    import numpy as np

    w = {n: np.load(args.dump)[n] for n in PARAM_NAMES}
    gpuenv.emit_row(status="ok", accuracy=accuracy(w))
    return 0


# ---------------------------------------------------------------------------
# parent
# ---------------------------------------------------------------------------


def run_ort_leg(args, n: int, rep: int, baseline_npz, dump: Path, errors: Path) -> dict:
    cmd = [sys.executable, str(Path(__file__).resolve()), "ort-child", "--n", str(n), "--steps", str(args.steps),
           "--baseline", str(baseline_npz) if baseline_npz else "", "--dump", str(dump)]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=gpuenv.child_env(), timeout=args.timeout)
    try:
        row = gpuenv.parse_row(proc.stdout)
    except Exception:
        row = {"status": "failed", "error": f"rc={proc.returncode}: {proc.stderr.strip()[-300:]}"}
    if row.get("status") != "ok":
        errors.mkdir(parents=True, exist_ok=True)
        (errors / f"ort_n{n}_r{rep}.log").write_text(
            f"cmd: {' '.join(cmd)}\nrc={proc.returncode}\n--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}\n"
        )
        if "out of memory" in (proc.stderr or "").lower():
            row["status"] = "oom"
    return row


def run_torch_leg(args, n: int, rep: int, out: Path, errors: Path) -> dict:
    """N torch tenants as OS processes: spawn (framework start-up, timed separately),
    then a synchronized GO barrier so the 50-step loops run concurrently."""
    dumps = {s: out / "dumps" / f"torch_n{n}_r{rep}_s{s}.npz" for s in sorted({0, n // 2, n - 1})}
    dumps[0] = out / "dumps" / f"torch_n{n}_r{rep}_s0.npz"
    for d in dumps.values():
        d.parent.mkdir(parents=True, exist_ok=True)
    t_spawn0 = time.perf_counter()
    procs = []
    for s in range(n):
        cmd = [sys.executable, str(Path(__file__).resolve()), "torch-child", "--steps", str(args.steps)]
        if s in dumps:
            cmd += ["--dump", str(dumps[s])]
        procs.append(
            subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, bufsize=1, env=gpuenv.child_env())
        )
    pids = [p.pid for p in procs]
    stdout_lines: list[list[str]] = [[] for _ in procs]

    def reader(i: int) -> None:
        for line in procs[i].stdout:
            stdout_lines[i].append(line)

    readers = [threading.Thread(target=reader, args=(i,), daemon=True) for i in range(n)]
    with gpuenv.SmiSampler(pids, interval=0.05) as sampler:
        for th in readers:
            th.start()
        # wait for every tenant's READY (with timeout); a dead process shows no READY
        deadline = time.perf_counter() + args.timeout
        while time.perf_counter() < deadline:
            ready = sum(any(l.strip() == "READY" for l in lines) for lines in stdout_lines)
            alive = sum(p.poll() is None for p in procs)
            if ready == n or (ready == alive and alive < n):
                break
            time.sleep(0.1)
        t_go = time.perf_counter()
        spawn_ready_s = t_go - t_spawn0
        for p in procs:
            try:
                if p.stdin:
                    p.stdin.write("\n")
                    p.stdin.flush()
            except (BrokenPipeError, OSError):
                pass  # tenant died before the barrier; recorded below
        # wall = GO -> last tenant finished its 50 steps and exited
        deadline = time.perf_counter() + args.timeout
        for p in procs:
            try:
                p.wait(timeout=max(1.0, deadline - time.perf_counter()))
            except subprocess.TimeoutExpired:
                p.kill()
        wall = time.perf_counter() - t_go
    for th in readers:
        th.join(timeout=5)
    total = [sum(s.get(pid, 0.0) for pid in pids) for _, s in sampler.samples]
    peak_total = max(total) if total else None
    oom, failed = False, []
    for s, p in enumerate(procs):
        stderr = p.stderr.read() if p.stderr else ""
        if p.returncode != 0:
            failed.append(f"tenant {s}: rc={p.returncode} {stderr.strip()[-200:]}")
            if "out of memory" in stderr.lower():
                oom = True
            errors.mkdir(parents=True, exist_ok=True)
            (errors / f"torch_n{n}_r{rep}_s{s}.log").write_text(
                f"rc={p.returncode}\n--- stdout ---\n{''.join(stdout_lines[s])}\n--- stderr ---\n{stderr}\n"
            )
    if failed:
        return {"status": "oom" if oom else "failed", "error": "; ".join(failed)[:400],
                "peak_mb": peak_total, "spawn_ready_s": spawn_ready_s}
    accs = {}
    for s, d in dumps.items():
        proc = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "eval-child", "--dump", str(d)],
            capture_output=True, text=True, env=gpuenv.child_env(), timeout=300,
        )
        try:
            accs[f"acc_s{s}"] = gpuenv.parse_row(proc.stdout)["accuracy"]
        except Exception:
            accs[f"acc_s{s}"] = None
    return {
        "status": "ok",
        "wall_s": wall,
        "aggregate_steps_per_s": n * args.steps / wall,
        "peak_mb": peak_total,
        "per_tenant_peak_mb": (peak_total / n) if peak_total else None,
        "spawn_ready_s": spawn_ready_s,
        **accs,
    }


COLUMNS = [
    "n", "leg", "rep", "status", "wall_s", "aggregate_steps_per_s", "create_s", "spawn_ready_s",
    "peak_mb", "resident_mb", "per_tenant_peak_mb", "pairwise_maxdiff", "baseline_maxdiff",
    "acc_s0", "acc_mid", "acc_last", "error",
]


def _flatten(row: dict, n: int, leg: str, rep: int) -> dict:
    mid, last = n // 2, n - 1
    return {
        "n": n, "leg": leg, "rep": rep,
        "status": row.get("status"),
        "wall_s": row.get("wall_s"),
        "aggregate_steps_per_s": row.get("aggregate_steps_per_s"),
        "create_s": row.get("create_s"),
        "spawn_ready_s": row.get("spawn_ready_s"),
        "peak_mb": row.get("peak_mb"),
        "resident_mb": row.get("resident_mb"),
        "per_tenant_peak_mb": row.get("per_tenant_peak_mb"),
        "pairwise_maxdiff": row.get("pairwise_maxdiff"),
        "baseline_maxdiff": row.get("baseline_maxdiff"),
        "acc_s0": row.get("acc_s0"),
        "acc_mid": row.get(f"acc_s{mid}"),
        "acc_last": row.get(f"acc_s{last}"),
        "error": row.get("error"),
    }


def parent_main(args) -> int:
    import csv

    gpuenv.ensure_loader_env(inprocess_ort=False)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    errors = out / "errors"
    dumps = out / "dumps"
    dumps.mkdir(parents=True, exist_ok=True)
    baseline_npz = dumps / "ort_baseline.npz"
    rows = []

    # Baseline: single ORT tenant, dumped final weights + accuracy reference.
    if not baseline_npz.exists() or args.rebaseline:
        print("[baseline] single ORT tenant ...", flush=True)
        row = run_ort_leg(args, 1, 0, None, baseline_npz, errors)
        if row.get("status") != "ok":
            print(f"[baseline] FAILED: {row.get('error')}")
            return 1
        print(f"[baseline] acc_s0={row.get('acc_s0')}  wall {row['wall_s']:.2f}s", flush=True)

    oom_ort = oom_torch = None
    ns = [int(x) for x in args.ns.split(",") if x]
    for n in ns:
        for rep in range(1, args.reps + 1):
            if oom_ort is None:
                row = run_ort_leg(args, n, rep, baseline_npz, dumps / f"ort_n{n}_r{rep}_s0.npz", errors)
                rows.append(_flatten(row, n, "ort", rep))
                if row.get("status") == "oom":
                    oom_ort = n
                print(
                    f"n{n:<3d} ort   r{rep} {row.get('status','?'):7s} agg {row.get('aggregate_steps_per_s') or 0:8.1f} steps/s "
                    f"peak {row.get('peak_mb') or 0:.0f} MiB ({(row.get('per_tenant_peak_mb') or 0):.0f}/tenant) "
                    f"baseΔ {row.get('baseline_maxdiff')}",
                    flush=True,
                )
            if oom_torch is None:
                row = run_torch_leg(args, n, rep, out, errors)
                rows.append(_flatten(row, n, "torch_proc", rep))
                if row.get("status") == "oom":
                    oom_torch = n
                print(
                    f"n{n:<3d} torch r{rep} {row.get('status','?'):7s} agg {row.get('aggregate_steps_per_s') or 0:8.1f} steps/s "
                    f"peak {row.get('peak_mb') or 0:.0f} MiB ({(row.get('per_tenant_peak_mb') or 0):.0f}/tenant) "
                    f"spawn {row.get('spawn_ready_s') or 0:.1f}s",
                    flush=True,
                )

    with (out / "t12d.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)

    summary_lines = []
    for n in ns:
        for leg in ("ort", "torch_proc"):
            cur = [r for r in rows if r["n"] == n and r["leg"] == leg and r["status"] == "ok"]
            if cur:
                agg = sorted(float(r["aggregate_steps_per_s"]) for r in cur)[len(cur) // 2]
                peak = sorted(float(r["peak_mb"]) for r in cur if r.get("peak_mb") is not None)
                accs = {r.get("acc_s0") for r in cur}
                line = (f"n{n:<3d} {leg:10s} agg {agg:8.1f} steps/s  peak {peak[len(peak)//2] if peak else 0:.0f} MiB  "
                        f"acc_s0 {accs}")
            else:
                st = [r["status"] for r in rows if r["n"] == n and r["leg"] == leg]
                line = f"n{n:<3d} {leg:10s} {st[0] if st else 'skipped'}"
            summary_lines.append(line)
            print("  " + line)
    (out / "t12d_summary.txt").write_text(
        "T12d multi-tenant LoRA (50 steps/tenant, same init, same batch order)\n"
        + "\n".join(summary_lines)
        + f"\nOOM: ort at {oom_ort}, torch_proc at {oom_torch}\n"
    )
    gpuenv.write_meta(out, {
        "subitem": "T12d",
        "framing": "N ORT InferenceSessions (one process/context) executing our LoRA graph vs N PyTorch "
                   "framework processes; per-tenant deployment cost on one GPU; throughput context only",
        "ns": ns, "steps_per_tenant": args.steps, "reps": args.reps,
        "graph": str(LORA_GRAPH), "oom_points": {"ort": oom_ort, "torch_proc": oom_torch},
    })
    print(f"[done] wrote {out}/t12d.csv, t12d_summary.txt, meta.json")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode")
    c = sub.add_parser("ort-child")
    c.add_argument("--n", type=int, required=True)
    c.add_argument("--steps", type=int, default=STEPS)
    c.add_argument("--baseline", default="")
    c.add_argument("--dump", default="")
    t = sub.add_parser("torch-child")
    t.add_argument("--steps", type=int, default=STEPS)
    t.add_argument("--dump", default="")
    e = sub.add_parser("eval-child")
    e.add_argument("--dump", required=True)
    p = sub.add_parser("run")
    p.add_argument("--ns", default=",".join(str(x) for x in NS))
    p.add_argument("--steps", type=int, default=STEPS)
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--timeout", type=float, default=1800.0)
    p.add_argument("--rebaseline", action="store_true")
    p.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    if args.mode == "ort-child":
        return ort_child(args)
    if args.mode == "torch-child":
        return torch_child(args)
    if args.mode == "eval-child":
        return eval_child(args)
    if args.mode == "run":
        return parent_main(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
