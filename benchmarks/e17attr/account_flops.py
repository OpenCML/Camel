# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T13 account 1: per-step FLOPs of the executed training-step graph, macro
(camel_nvm) vs macro+simplify (camel_opt_nvm), for mlp_big and lenet_big.

Method. The operators executed per timed step and their call counts are the
measured ground truth from run_opstats.py (op-stats table / executed steps;
verified integral). Each call site's shape is static (fixed benchmark), so each
executed op instance gets an exact FLOP count under the stated model:

  matmul / matmul_grad_lhs / matmul_grad_rhs   2*M*N*K (grad_lhs: dX=dy@W^T,
                                               grad_rhs: dW=X^T@dy; same 2MNK)
  conv2d / conv2d_input_grad / conv2d_kernel_grad
                                               2*N*Cin*Cout*K^2*OH*OW
                                               (kernel grad reduces over N; same count)
  conv2d_bias_grad                             N*Cout*OH*OW
  add / multiply / relu / gt (elementwise)     numel(output)
  sum_to                                       numel(input)
  mean                                         numel(input)
  max_pool2d                                   K^2 * numel(output)
  max_pool2d_grad                              numel(output) (scatter)
  flatten / reshape / broadcast_like           numel(output)
  shape / numel / scalar (:op/div_d, :op/ltod) 0
  nn:sgd (not wrapped by op-stats; fused)      2 * numel(params)

The script asserts every op instance count equals the measured calls/step from
--opstats (run_opstats.py output), so the table is machine-checked against
execution. Output: per model, per config, per op: calls/step, FLOPs/step, and
the eliminated-op list with per-op FLOPs (paper S5.5 material).
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

# ---------------------------------------------------------------- shapes
# mlp_big: x[256,784] -> fc1[784,1024] -> fc2[1024,512] -> fc3[512,10]
# lenet_big: x[128,1,28,28] -> conv1[16,1,5,5] p2 s1 -> [128,16,28,28]
#   -> pool2 -> [128,16,14,14] -> conv2[48,16,5,5] p0 s1 -> [128,48,10,10]
#   -> pool2 -> [128,48,5,5] -> flatten[128,1200] -> fc1[1200,120] -> fc2[120,84] -> fc3[84,10]


def gemm(m, n, k):
    return 2 * m * n * k


def conv(n, cin, cout, k, oh, ow):
    return 2 * n * cin * cout * k * k * oh * ow


# (op, flops_per_call, kept_by_opt, note)
MLP = [
    # forward (kept)
    ("matmul", gemm(256, 1024, 784), True, "fc1 y=x@W1 [256,784]@[784,1024]"),
    ("matmul", gemm(256, 512, 1024), True, "fc2 [256,1024]@[1024,512]"),
    ("matmul", gemm(256, 10, 512), True, "fc3 [256,512]@[512,10]"),
    ("add", 256 * 1024, True, "fc1 bias"),
    ("add", 256 * 512, True, "fc2 bias"),
    ("add", 256 * 10, True, "fc3 bias"),
    ("relu", 256 * 1024, True, "relu1"),
    ("relu", 256 * 512, True, "relu2"),
    ("multiply", 256 * 10, True, "loss y*y"),
    ("mean", 256 * 10, True, "loss mean"),
    # backward of loss (eliminated)
    ("broadcast_like", 256 * 10, False, "mean bwd: seed broadcast to [256,10]"),
    ("numel", 0, False, "mean bwd: numel(y)"),
    ("multiply", 256 * 10, False, "mean bwd: scale by 1/numel"),
    ("multiply", 256 * 10, False, "y*y bwd: gy*y"),
    ("add", 256 * 10, False, "y*y bwd: gy*y + gy*y"),
    ("sum_to", 256 * 10, False, "y*y bwd: accumulate into y (opnd 1)"),
    ("sum_to", 256 * 10, False, "y*y bwd: accumulate into y (opnd 2)"),
    # backward of forward.pb (eliminated)
    ("matmul_grad_lhs", gemm(256, 512, 10), False, "fc3 dX = dy@W3^T"),
    ("matmul_grad_rhs", gemm(512, 10, 256), False, "fc3 dW = h2^T@dy"),
    ("matmul_grad_lhs", gemm(256, 1024, 512), False, "fc2 dX"),
    ("matmul_grad_rhs", gemm(1024, 512, 256), False, "fc2 dW"),
    ("matmul_grad_lhs", gemm(256, 784, 1024), False, "fc1 dX"),
    ("matmul_grad_rhs", gemm(784, 1024, 256), False, "fc1 dW"),
    ("gt", 256 * 512, False, "relu2 bwd mask"),
    ("gt", 256 * 1024, False, "relu1 bwd mask"),
    ("multiply", 256 * 512, False, "relu2 bwd apply mask"),
    ("multiply", 256 * 1024, False, "relu1 bwd apply mask"),
    ("sum_to", 256 * 10, False, "fc3 add bwd: pass to mm out"),
    ("sum_to", 256 * 10, False, "fc3 add bwd: reduce to bias [10]"),
    ("sum_to", 256 * 512, False, "fc2 add bwd: pass to mm out"),
    ("sum_to", 256 * 512, False, "fc2 add bwd: reduce to bias [512]"),
    ("sum_to", 256 * 1024, False, "fc1 add bwd: pass to mm out"),
    ("sum_to", 256 * 1024, False, "fc1 add bwd: reduce to bias [1024]"),
    # SGD update (eliminated; fused kernel, invisible to op-stats)
    ("nn:sgd", 2 * (784 * 1024 + 1024 + 1024 * 512 + 512 + 512 * 10 + 10), False, "sgd over 6 param tensors"),
]

LENET = [
    # forward (kept)
    ("conv2d", conv(128, 1, 16, 5, 28, 28), True, "conv1 [128,1,28,28]*[16,1,5,5] p2"),
    ("conv2d", conv(128, 16, 48, 5, 10, 10), True, "conv2 [128,16,14,14]*[48,16,5,5]"),
    ("max_pool2d", 4 * 128 * 16 * 14 * 14, True, "pool1 k2"),
    ("max_pool2d", 4 * 128 * 48 * 5 * 5, True, "pool2 k2"),
    ("relu", 128 * 16 * 28 * 28, True, "relu conv1 out"),
    ("relu", 128 * 48 * 10 * 10, True, "relu conv2 out"),
    ("relu", 128 * 120, True, "relu fc1 out"),
    ("relu", 128 * 84, True, "relu fc2 out"),
    ("flatten", 128 * 1200, True, "flatten"),
    ("matmul", gemm(128, 120, 1200), True, "fc1"),
    ("matmul", gemm(128, 84, 120), True, "fc2"),
    ("matmul", gemm(128, 10, 84), True, "fc3"),
    ("add", 128 * 120, True, "fc1 bias"),
    ("add", 128 * 84, True, "fc2 bias"),
    ("add", 128 * 10, True, "fc3 bias"),
    ("multiply", 128 * 10, True, "loss y*y"),
    ("mean", 128 * 10, True, "loss mean"),
    # backward of loss (eliminated)
    ("broadcast_like", 128 * 10, False, "mean bwd seed"),
    ("numel", 0, False, "mean bwd numel"),
    ("multiply", 128 * 10, False, "mean bwd scale"),
    ("multiply", 128 * 10, False, "y*y bwd gy*y"),
    ("add", 128 * 10, False, "y*y bwd g+g"),
    ("sum_to", 128 * 10, False, "y*y bwd acc y (1)"),
    ("sum_to", 128 * 10, False, "y*y bwd acc y (2)"),
    # backward of fc stack (eliminated)
    ("matmul_grad_lhs", gemm(128, 84, 10), False, "fc3 dX"),
    ("matmul_grad_rhs", gemm(84, 10, 128), False, "fc3 dW"),
    ("matmul_grad_lhs", gemm(128, 120, 84), False, "fc2 dX"),
    ("matmul_grad_rhs", gemm(120, 84, 128), False, "fc2 dW"),
    ("matmul_grad_lhs", gemm(128, 1200, 120), False, "fc1 dX"),
    ("matmul_grad_rhs", gemm(1200, 120, 128), False, "fc1 dW"),
    ("gt", 128 * 84, False, "relu fc2 bwd mask"),
    ("gt", 128 * 120, False, "relu fc1 bwd mask"),
    ("gt", 128 * 48 * 10 * 10, False, "relu conv2 bwd mask"),
    ("gt", 128 * 16 * 28 * 28, False, "relu conv1 bwd mask"),
    ("multiply", 128 * 84, False, "relu fc2 bwd apply"),
    ("multiply", 128 * 120, False, "relu fc1 bwd apply"),
    ("multiply", 128 * 48 * 10 * 10, False, "relu conv2 bwd apply"),
    ("multiply", 128 * 16 * 28 * 28, False, "relu conv1 bwd apply"),
    ("sum_to", 128 * 10, False, "fc3 add bwd pass"),
    ("sum_to", 128 * 10, False, "fc3 add bwd bias"),
    ("sum_to", 128 * 84, False, "fc2 add bwd pass"),
    ("sum_to", 128 * 84, False, "fc2 add bwd bias"),
    ("sum_to", 128 * 120, False, "fc1 add bwd pass"),
    ("sum_to", 128 * 120, False, "fc1 add bwd bias"),
    ("shape", 0, False, "flatten bwd: shape of input"),
    ("reshape", 128 * 1200, False, "flatten bwd: reshape grad"),
    # backward of conv stack (eliminated)
    ("max_pool2d_grad", 128 * 48 * 5 * 5, False, "pool2 bwd scatter"),
    ("max_pool2d_grad", 128 * 16 * 14 * 14, False, "pool1 bwd scatter"),
    ("conv2d_input_grad", conv(128, 16, 48, 5, 10, 10), False, "conv2 dX"),
    ("conv2d_kernel_grad", conv(128, 16, 48, 5, 10, 10), False, "conv2 dW"),
    ("conv2d_bias_grad", 128 * 48 * 10 * 10, False, "conv2 db"),
    ("conv2d_input_grad", conv(128, 1, 16, 5, 28, 28), False, "conv1 dX"),
    ("conv2d_kernel_grad", conv(128, 1, 16, 5, 28, 28), False, "conv1 dW"),
    ("conv2d_bias_grad", 128 * 16 * 28 * 28, False, "conv1 db"),
    # SGD update (eliminated)
    ("nn:sgd", 2 * (16 * 25 + 16 + 48 * 16 * 25 + 48 + 1200 * 120 + 120 + 120 * 84 + 84 + 84 * 10 + 10), False, "sgd over 10 param tensors"),
]

MODELS = {"mlp_big": MLP, "lenet_big": LENET}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--opstats", type=Path, required=True, help="run_opstats.py's opstats.csv")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    # measured calls/step per op, averaged over trials, per (model, config)
    measured: dict = {}
    with open(args.opstats) as f:
        for row in csv.DictReader(f):
            key = (row["model"], row["config"])
            ops = json.loads(row["ops_json"])
            m = measured.setdefault(key, {})
            for op, v in ops.items():
                m.setdefault(op, []).append(v["calls_per_step"])

    ok = True
    summary_rows, detail_rows = [], []
    for model, table in MODELS.items():
        # expected calls/step per op from the static table
        expect: dict[str, float] = {}
        for op, _f, _k, _n in table:
            expect[op] = expect.get(op, 0) + 1
        got_nvm = {op: sum(vs) / len(vs) for op, vs in measured.get((model, "camel_nvm"), {}).items()}
        got_opt = {op: sum(vs) / len(vs) for op, vs in measured.get((model, "camel_opt_nvm"), {}).items()}
        for op, cnt in sorted(expect.items()):
            if op == "nn:sgd":
                continue  # not wrapped by op-stats
            g = got_nvm.get(op, 0.0)
            if abs(g - cnt) > 1e-6:
                print(f"COUNT MISMATCH {model} nvm {op}: table {cnt}/step, measured {g}/step")
                ok = False
        for op in set(got_nvm) - set(expect) - {"load_npy", "set_num_threads"}:
            print(f"UNEXPECTED OP {model} nvm: {op} x{got_nvm[op]}/step")
            ok = False
        for op, g in sorted(got_opt.items()):
            if op in {"load_npy", "set_num_threads"}:
                continue
            kept = expect.get(op, 0) and any(k for o, _f, k, _n in table if o == op)
            want = sum(1 for o, _f, k, _n in table if o == op and k)
            if abs(g - want) > 1e-6:
                print(f"COUNT MISMATCH {model} opt {op}: kept {want}/step, measured {g}/step")
                ok = False

        for op, flops, kept, note in table:
            detail_rows.append(
                {"model": model, "op": op, "executed_by": "nvm+opt" if kept else "nvm only (eliminated)",
                 "calls_per_step": 1, "flops_per_step": flops, "note": note}
            )
        fwd = sum(f for _o, f, k, _n in table if k)
        elim = sum(f for _o, f, k, _n in table if not k)
        elim_gemm = sum(f for o, f, k, _n in table if not k and ("matmul_grad" in o or "conv2d_input" in o or "conv2d_kernel" in o))
        elim_sgd = sum(f for o, f, k, _n in table if not k and o == "nn:sgd")
        elim_other = elim - elim_gemm - elim_sgd
        summary_rows.append(
            {"model": model, "flops_nvm": fwd + elim, "flops_opt": fwd, "flops_eliminated": elim,
             "elim_gemm_bwd": elim_gemm, "elim_sgd": elim_sgd, "elim_elementwise": elim_other,
             "ratio_nvm_over_opt": round((fwd + elim) / fwd, 4), "opt_share_pct": round(100.0 * fwd / (fwd + elim), 2)}
        )
        s = summary_rows[-1]
        print(
            f"{model:10s} nvm {s['flops_nvm']/1e6:8.2f} MFLOP/step  opt {s['flops_opt']/1e6:8.2f}  "
            f"eliminated {s['flops_eliminated']/1e6:8.2f} ({100.0*elim/(fwd+elim):.1f}%)  ratio x{s['ratio_nvm_over_opt']:.3f}"
        )

    with open(args.out / "flops_detail.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["model", "op", "executed_by", "calls_per_step", "flops_per_step", "note"])
        w.writeheader()
        w.writerows(detail_rows)
    with open(args.out / "flops_summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(summary_rows[0].keys()))
        w.writeheader()
        w.writerows(summary_rows)
    print(f"[done] wrote flops_detail.csv / flops_summary.csv; count check {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
