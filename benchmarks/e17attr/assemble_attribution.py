# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T13 acceptance core: assemble the per-model attribution tables from the raw
measurements and check that the parts sum to the measured step times (<15%).

Reads (under --raw):
  train_summary.csv            headline interleaved step times (train.py --time)
  opstats/opstats.csv          camel per-op kernel ms/step + step (run_opstats.py)
  torch_split/torch_step_split.csv
                               torch fwd/bwd/sgd wall segments (torch_step_split.py)
  micro/micro.csv              per-shape kernel boundary numbers (run_micro.py)
  flops/flops_summary.csv      FLOPs per config (account_flops.py)

Per model the table answers: eager step = kernel + framework; camel_nvm step =
kernel(kept) + kernel(eliminated) + sgd + interpreter; camel_opt step =
kernel(opt graph). Checks:

  C1 torch segments tile the step:        (fwd+bwd+sgd) vs plain step
  C2 camel_nvm parts sum to the step:     kernel + nonkernel vs step (definitional,
                                          reported; the informative half is C3)
  C3 nvm nonkernel ~= sgd micro (nn:sgd is not op-stats-wrapped; the residual
                                          after sgd is the interpreter share)
  C4 speedup closure: (1 + bwd/fwd + sgd/fwd) / kappa  vs  measured
                                          torch_full / camel_opt_step, where
                                          kappa = camel_opt_step / torch_fwd
                                          (same kept work: forward+loss)
  C5 camel_opt kernel ~= torch fwd x (micro fwd-sum ratio)  [kernel boundary
                                          on the kept work, from micro.csv]
  C6 nvm-vs-torch step ratio vs micro full-work kernel ratio [boundary context]

Writes attribution.md (both tables + checks) and attribution.csv.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from pathlib import Path

FWD_OPS_MLP = {"matmul", "add", "relu", "multiply", "mean"}
GRAD_GEMM = {"matmul_grad_lhs", "matmul_grad_rhs"}
CONV_GRAD = {"conv2d_input_grad", "conv2d_kernel_grad", "conv2d_bias_grad"}
SETUP = {"load_npy", "set_num_threads"}

MLP_SHAPES = ["mlp_fc1", "mlp_fc2", "mlp_fc3"]
LENET_SHAPES = ["lenet_conv1", "lenet_conv2", "lenet_fc1", "lenet_fc2", "lenet_fc3"]


def med(xs):
    return statistics.median(list(xs))


def load_raw(raw: Path) -> dict:
    d = {}
    with open(raw / "train_summary.csv") as f:
        d["headline"] = {(r["model"], r["config"]): r for r in csv.DictReader(f)}
    op = {}
    with open(raw / "opstats" / "opstats.csv") as f:
        for r in csv.DictReader(f):
            op.setdefault((r["model"], r["config"]), []).append(r)
    d["opstats"] = op
    sp = {}
    with open(raw / "torch_split" / "torch_step_split.csv") as f:
        for r in csv.DictReader(f):
            sp.setdefault(r["model"], []).append(r)
    d["split"] = sp
    mi = {}
    with open(raw / "micro" / "micro.csv") as f:
        for r in csv.DictReader(f):
            mi.setdefault(r["shape"], []).append(r)
    d["micro"] = mi
    with open(raw / "flops" / "flops_summary.csv") as f:
        d["flops"] = {r["model"]: r for r in csv.DictReader(f)}
    # kept-call share per op name (multiply/add have both forward and backward
    # call sites; every other op is cleanly all-kept or all-eliminated)
    share: dict[str, dict[str, float]] = {}
    with open(raw / "flops" / "flops_detail.csv") as f:
        for r in csv.DictReader(f):
            m = share.setdefault(r["model"], {})
            tot, kept = m.get(r["op"], (0, 0))
            m[r["op"]] = (tot + 1, kept + (1 if r["executed_by"] == "nvm+opt" else 0))
    d["kept_share"] = {m: {op: k / t for op, (t, k) in ops.items()} for m, ops in share.items()}
    return d


def camel_side(d: dict, model: str) -> dict:
    out = {}
    for config in ["camel_nvm", "camel_opt_nvm"]:
        rows = d["opstats"][(model, config)]
        step = med(float(r["step_median_ms"]) for r in rows)
        kernel = med(float(r["kernel_per_step_ms"]) for r in rows)
        kernel_d = med(float(r["kernel_diff_ms"]) for r in rows)
        per_trial_ops = [json.loads(r["ops_json"]) for r in rows]
        ops: dict[str, list[float]] = {}
        for po in per_trial_ops:
            for name, v in po.items():
                ops.setdefault(name, []).append(v["ms_per_step"])
        ops_med = {k: med(v) for k, v in ops.items()}

        def cat_ms(names, _per_trial_ops=per_trial_ops) -> float:
            """median over trials of the per-trial sum over op names (sum-then-median)."""
            pred = names if callable(names) else lambda n: n in names
            return med(sum(po.get(n, {"ms_per_step": 0.0})["ms_per_step"] for n in po if pred(n)) for po in _per_trial_ops)

        out[config] = {
            "step": step, "kernel": kernel, "kernel_diff": kernel_d, "nonkernel": step - kernel,
            "ops": ops_med, "cat_ms": cat_ms,
        }
    return out


def torch_side(d: dict, model: str) -> dict:
    rows = d["split"][model]
    return {k: med(float(r[k]) for r in rows) for k in ["full_ms", "fwd_ms", "bwd_ms", "sgd_ms", "plain_ms"]}


def micro_side(d: dict, shapes: list[str]) -> dict:
    out = {}
    for s in shapes:
        rows = d["micro"][s]
        kind = rows[0]["kind"]
        if kind == "sgd":
            out[s] = {"camel_sgd_us": med(float(r["camel_fwd_us"]) for r in rows)}
        else:
            out[s] = {
                "camel_fwd_us": med(float(r["camel_fwd_us"]) for r in rows),
                "camel_bwd_us": med(float(r["camel_bwd_us"]) for r in rows),
                "torch_fwd_us": med(float(r["torch_fwd_us"]) for r in rows),
                "torch_bwd_us": med(float(r["torch_bwd_us"]) for r in rows),
            }
    return out


def attribute(model: str, d: dict) -> dict:
    cam = camel_side(d, model)
    tor = torch_side(d, model)
    shapes = MLP_SHAPES if model == "mlp_big" else LENET_SHAPES
    mic = micro_side(d, shapes + [f"{'mlp' if model == 'mlp_big' else 'lenet'}_sgd"])
    sgd_us = mic[f"{'mlp' if model == 'mlp_big' else 'lenet'}_sgd"]["camel_sgd_us"]

    nvm, opt = cam["camel_nvm"], cam["camel_opt_nvm"]
    share = d["kept_share"][model]
    elim_gemm = nvm["cat_ms"](lambda n: n in GRAD_GEMM or n in CONV_GRAD)
    kept_nvm = nvm["cat_ms"](lambda n: n in opt["ops"] and share.get(n, 1.0) == 1.0)
    kept_nvm += sum(nvm["ops"].get(n, 0.0) * s for n, s in share.items() if 0.0 < s < 1.0 and n in opt["ops"])
    gemm_fwd_opt = opt["cat_ms"](lambda n: n in {"matmul", "conv2d"})
    other_elim = nvm["kernel"] - kept_nvm - elim_gemm

    headline = d["headline"]
    h_torch = float(headline[(model, "torch_eager")]["median_ms"])
    h_nvm = float(headline[(model, "camel_nvm")]["median_ms"])
    h_opt = float(headline[(model, "camel_opt_nvm")]["median_ms"])

    seg_sum = tor["fwd_ms"] + tor["bwd_ms"] + tor["sgd_ms"]
    c1 = seg_sum / tor["plain_ms"] - 1.0

    interp_nvm = nvm["nonkernel"] - sgd_us / 1e3
    c3 = (nvm["nonkernel"] - sgd_us / 1e3) / nvm["step"]

    kappa = opt["step"] / tor["fwd_ms"]
    work_ratio = seg_sum / tor["fwd_ms"]
    speed_pred = work_ratio / kappa
    speed_meas = tor["full_ms"] / opt["step"]
    c4 = speed_pred / speed_meas - 1.0

    micro_camel_fwd = sum(mic[s]["camel_fwd_us"] for s in shapes) / 1e3
    micro_torch_fwd = sum(mic[s]["torch_fwd_us"] for s in shapes) / 1e3
    # apples-to-apples: opt's GEMM/conv subtotal vs the same kernels' micro sum
    c5 = gemm_fwd_opt / micro_camel_fwd - 1.0
    c5b = opt["kernel"] / micro_torch_fwd - 1.0  # context: whole opt kernel vs torch micro fwd

    flops = d["flops"][model]
    return {
        "model": model,
        "torch": tor,
        "camel_nvm": nvm,
        "camel_opt": opt,
        "nvm_kept_kernel": kept_nvm,
        "nvm_elim_gemm": elim_gemm,
        "nvm_elim_other": other_elim,
        "opt_gemm_fwd": gemm_fwd_opt,
        "sgd_micro_ms": sgd_us / 1e3,
        "interp_nvm_ms": interp_nvm,
        "headline": {"torch": h_torch, "nvm": h_nvm, "opt": h_opt},
        "kappa": kappa,
        "work_ratio": work_ratio,
        "speed_pred": speed_pred,
        "speed_meas": speed_meas,
        "checks": {"C1_seg_tiling": c1, "C3_interp_share_after_sgd": c3, "C4_speedup_closure": c4, "C5_opt_gemm_vs_micro": c5, "C5b_opt_kernel_vs_torch_micro": c5b},
        "micro": mic,
        "micro_sums": {"camel_fwd_ms": micro_camel_fwd, "torch_fwd_ms": micro_torch_fwd},
        "flops": flops,
    }


def fmt_table(a: dict) -> str:
    t, nvm, opt, h = a["torch"], a["camel_nvm"], a["camel_opt"], a["headline"]
    L = []
    L.append(f"### {a['model']} — attribution (medians over trials, this machine, 4 threads)\n")
    L.append("| component | ms/step | source |")
    L.append("|---|---:|---|")
    L.append(f"| torch eager step (segmented) | {t['full_ms']:.3f} | torch_step_split |")
    L.append(f"| &nbsp;&nbsp;forward+loss | {t['fwd_ms']:.3f} | segment |")
    L.append(f"| &nbsp;&nbsp;backward | {t['bwd_ms']:.3f} | segment |")
    L.append(f"| &nbsp;&nbsp;SGD update | {t['sgd_ms']:.3f} | segment |")
    L.append(f"| torch eager step (plain, x-check) | {t['plain_ms']:.3f} | torch_step_split |")
    L.append(f"| torch eager step (headline protocol) | {h['torch']:.3f} | train.py --time |")
    L.append(f"| **camel_nvm step** | **{nvm['step']:.3f}** | run_opstats (headline {h['nvm']:.3f}) |")
    L.append(f"| &nbsp;&nbsp;kernel: kept ops (as in opt) | {a['nvm_kept_kernel']:.3f} | opstats per-op |")
    L.append(f"| &nbsp;&nbsp;kernel: eliminated GEMM/conv grads | {a['nvm_elim_gemm']:.3f} | opstats per-op |")
    L.append(f"| &nbsp;&nbsp;kernel: eliminated other (gt/sum_to/mul/pool_grad/…) | {a['nvm_elim_other']:.3f} | opstats per-op |")
    L.append(f"| &nbsp;&nbsp;nn:sgd (not op-stats-wrapped) | {a['sgd_micro_ms']:.3f} | micro sgd |")
    L.append(f"| &nbsp;&nbsp;interpreter residual | {a['interp_nvm_ms']:.3f} | step − kernel − sgd |")
    L.append(f"| **camel_opt step** | **{opt['step']:.3f}** | run_opstats (headline {h['opt']:.3f}) |")
    L.append(f"| &nbsp;&nbsp;kernel (opt graph: forward+loss) | {opt['kernel']:.3f} | opstats |")
    L.append(f"| &nbsp;&nbsp;non-kernel | {opt['nonkernel']:.3f} | step − kernel |")
    L.append("")
    L.append("| check | value | error |")
    L.append("|---|---|---:|")
    L.append(f"| C1 torch segments tile the step | fwd+bwd+sgd = {t['fwd_ms']+t['bwd_ms']+t['sgd_ms']:.3f} vs plain {t['plain_ms']:.3f} | {100*a['checks']['C1_seg_tiling']:+.1f}% |")
    L.append(f"| C2 nvm parts sum to step | kernel {nvm['kernel']:.3f} + nonkernel {nvm['nonkernel']:.3f} = {nvm['kernel']+nvm['nonkernel']:.3f} | exact by construction |")
    L.append(f"| C3 nvm nonkernel = sgd + interpreter | {nvm['nonkernel']:.3f} = {a['sgd_micro_ms']:.3f} + {a['interp_nvm_ms']:.3f} (share {100*a['checks']['C3_interp_share_after_sgd']:+.1f}%) | — |")
    L.append(f"| C4 speedup closure: work_ratio/κ vs measured | {a['work_ratio']:.3f}/{a['kappa']:.3f} = {a['speed_pred']:.3f} vs {a['speed_meas']:.3f} | {100*a['checks']['C4_speedup_closure']:+.1f}% |")
    L.append(f"| C5 opt GEMM/conv subtotal vs Σ micro camel fwd | {a['opt_gemm_fwd']:.3f} vs {a['micro_sums']['camel_fwd_ms']:.3f} | {100*a['checks']['C5_opt_gemm_vs_micro']:+.1f}% |")
    L.append(f"| C5b opt whole kernel vs Σ micro torch fwd (context) | {opt['kernel']:.3f} vs {a['micro_sums']['torch_fwd_ms']:.3f} | {100*a['checks']['C5b_opt_kernel_vs_torch_micro']:+.1f}% |")
    L.append(f"| FLOPs nvm/opt (machine-independent) | ×{a['flops']['ratio_nvm_over_opt']} | — |")
    L.append("")
    return "\n".join(L)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    d = load_raw(args.raw)
    args.out.mkdir(parents=True, exist_ok=True)
    parts = []
    rows = []
    for model in ["mlp_big", "lenet_big"]:
        a = attribute(model, d)
        parts.append(fmt_table(a))
        rows.append(
            {
                "model": model,
                "torch_full_ms": round(a["torch"]["full_ms"], 4),
                "torch_fwd_ms": round(a["torch"]["fwd_ms"], 4),
                "torch_bwd_ms": round(a["torch"]["bwd_ms"], 4),
                "torch_sgd_ms": round(a["torch"]["sgd_ms"], 4),
                "nvm_step_ms": round(a["camel_nvm"]["step"], 4),
                "nvm_kernel_ms": round(a["camel_nvm"]["kernel"], 4),
                "nvm_kept_ms": round(a["nvm_kept_kernel"], 4),
                "nvm_elim_gemm_ms": round(a["nvm_elim_gemm"], 4),
                "nvm_elim_other_ms": round(a["nvm_elim_other"], 4),
                "sgd_ms": round(a["sgd_micro_ms"], 4),
                "interp_ms": round(a["interp_nvm_ms"], 4),
                "opt_step_ms": round(a["camel_opt"]["step"], 4),
                "opt_kernel_ms": round(a["camel_opt"]["kernel"], 4),
                "kappa": round(a["kappa"], 4),
                "work_ratio": round(a["work_ratio"], 4),
                "speed_pred": round(a["speed_pred"], 4),
                "speed_meas": round(a["speed_meas"], 4),
                "C1": round(a["checks"]["C1_seg_tiling"], 4),
                "C4": round(a["checks"]["C4_speedup_closure"], 4),
                "C5": round(a["checks"]["C5_opt_gemm_vs_micro"], 4),
            }
        )
        # eliminated-op ledger (paper S5.5): FLOPs per eliminated op instance
        # (flops_detail) joined with measured ms/step per op name (opstats nvm)
        nvm_ops = a["camel_nvm"]["ops"]
        share = d["kept_share"][model]
        with open(args.raw / "flops" / "flops_detail.csv") as f:
            details = [r for r in csv.DictReader(f) if r["model"] == model]
        elim_rows: dict[str, dict] = {}
        for r in details:
            op, kept, fl = r["op"], r["executed_by"] == "nvm+opt", int(r["flops_per_step"])
            e = elim_rows.setdefault(op, {"calls": 0, "flops": 0, "kept_calls": 0, "notes": []})
            e["calls"] += 1
            e["flops"] += fl
            e["kept_calls"] += 1 if kept else 0
            e["notes"].append(r["note"])
        elim_out = []
        nvm_step = a["camel_nvm"]["step"]
        for op, e in elim_rows.items():
            if e["kept_calls"] == e["calls"]:
                continue
            # time share of the eliminated call sites of this op name
            frac = (e["calls"] - e["kept_calls"]) / e["calls"]
            ms = nvm_ops.get(op, 0.0) * frac
            if op == "nn:sgd":
                ms = a["sgd_micro_ms"]
            elim_out.append(
                {
                    "model": model,
                    "op": op,
                    "eliminated_calls_per_step": e["calls"] - e["kept_calls"],
                    "eliminated_flops_per_step": e["flops"] - sum(
                        int(r["flops_per_step"]) for r in details if r["op"] == op and r["executed_by"] == "nvm+opt"
                    ),
                    "measured_ms_per_step": round(ms, 4),
                    "pct_of_nvm_step": round(100.0 * ms / nvm_step, 2),
                    "why_dead": "feeds only the discarded SGD update (backward of loss/forward)"
                    if op != "nn:sgd"
                    else "update result `next` unused in train_step; pure kernel on the sync control chain",
                }
            )
        elim_out.sort(key=lambda r: -r["measured_ms_per_step"])
        with open(args.out / f"eliminated_ops_{model}.csv", "w", newline="") as f:
            w = csv.DictWriter(
                f, fieldnames=["model", "op", "eliminated_calls_per_step", "eliminated_flops_per_step",
                               "measured_ms_per_step", "pct_of_nvm_step", "why_dead"])
            w.writeheader()
            w.writerows(elim_out)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "attribution.md").write_text("\n".join(parts))
    with open(args.out / "attribution.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print("\n".join(parts))
    print(f"[done] wrote {args.out / 'attribution.md'} / attribution.csv")
    return 0


if __name__ == "__main__":
    main()
