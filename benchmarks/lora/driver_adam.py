# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T3 three-way on-device LoRA fine-tuning driver, in-graph Adam (MLSys 2027 eval).

Same protocol as driver.py (E9, SGD): rank-8 LoRA on the MLP backbone, backbone
frozen, 640 steps over 32 batches of 32 rotated-MNIST user samples, identical
init — but the optimizer is Adam (lr 0.001, betas (0.9, 0.999), eps 1e-8):

  camel-native  benchmarks/lora/lora_mlp.cml train_ce_adam (subprocess)
  ort           this file drives lora_ce_step_adam.onnx in an *unmodified*
                ONNX Runtime InferenceSession, feeding updated adapters AND
                the optimizer state (adam_m_*/adam_v_*/adam_t) back each step
  torch         a plain PyTorch LoRA reference loop with torch.optim.Adam

Outputs (benchmarks/lora/out/report_adam/):
  curves.csv        step, loss per party, eval accuracy at epoch boundaries
  errors.csv        per-step pairwise |dloss|; final pairwise max|dW| per adapter
  summary.txt       headline numbers (accuracy before/after, worst errors)
"""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
A = ROOT / "artifacts" / "lora"
OUT = ROOT / "lora" / "out" / "report_adam"
GRAPH = ROOT / "lora" / "out" / "lora_ce_step_adam.onnx"
STEPS, NB, EPOCH = 640, 32, 32
LR = float(os.environ.get("CAMEL_FT_LR", "0.001"))
PARAM_NAMES = [
    "fc1_w", "fc1_b", "fc1_lora_a", "fc1_lora_b",
    "fc2_w", "fc2_b", "fc2_lora_a", "fc2_lora_b",
    "fc3_w", "fc3_b", "fc3_lora_a", "fc3_lora_b",
]
ADAPTERS = [n for n in PARAM_NAMES if "lora" in n]


def load_init() -> dict:
    return {n: np.load(A / "pretrained" / f"{n}.npy") for n in PARAM_NAMES}


def batches(i: int):
    d = A / "user_batches"
    return np.load(d / f"batch_{i % NB}.npy"), np.load(d / f"target_{i % NB}.npy")


def accuracy(w: dict) -> float:
    """Shared eval for all parties: LoRA forward on the rotated eval set (torch)."""
    import torch

    x = torch.from_numpy(np.load(A / "user_eval.npy"))
    y = np.load(A / "user_eval_target.npy")
    h = x
    for n in ("fc1", "fc2", "fc3"):
        h = h @ torch.from_numpy(w[f"{n}_w"]) + (h @ torch.from_numpy(w[f"{n}_lora_b"])) @ torch.from_numpy(
            w[f"{n}_lora_a"]
        ) * 2.0 + torch.from_numpy(w[f"{n}_b"])
        if n != "fc3":
            h = torch.relu(h)
    return float((h.argmax(1).numpy() == y).mean())


def camel_env(workload: str, extra: dict | None = None) -> dict:
    env = dict(os.environ)
    env.update(
        {
            "CAMEL_HOME": str(ROOT.parent / "out" / "latest"),
            "CAMEL_PACKAGES": str(ROOT / "common"),
            "CAMEL_BENCH_WEIGHTS": str(A / "pretrained"),
            "CAMEL_BENCH_INPUT": str(A / "input.npy"),
            "CAMEL_FT_BATCHES": str(A / "user_batches"),
            "CAMEL_BENCH_WORKLOAD": workload,
            "CAMEL_FT_LR": str(LR),
        }
    )
    env.update(extra or {})
    return env


def export_graph() -> None:
    print(f"[export] {GRAPH.name} ...")
    proc = subprocess.run(
        [str(ROOT.parent / "out" / "latest" / "bin" / "camel.exe"), str(ROOT / "lora" / "lora_mlp.cml")],
        capture_output=True,
        text=True,
        env=camel_env("export_ce_adam", {"CAMEL_BENCH_EXPORT": str(GRAPH)}),
        cwd=ROOT / "lora",
    )
    if proc.returncode != 0:
        raise RuntimeError(f"export failed: {proc.stderr[-2000:]}")
    import onnx

    onnx.checker.check_model(onnx.load(str(GRAPH)), full_check=True)
    print("[export] onnx.checker full_check: PASS")


def run_ort() -> tuple[list, dict]:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(GRAPH), providers=["CPUExecutionProvider"])
    outs = [o.name for o in sess.get_outputs()]
    w = load_init()
    m = {n: np.zeros_like(w[n]) for n in PARAM_NAMES}
    v = {n: np.zeros_like(w[n]) for n in PARAM_NAMES}
    t = np.array(0, dtype=np.int64)
    log = []
    for i in range(STEPS):
        x, tt = batches(i)
        feeds = {n: w[n] for n in PARAM_NAMES}
        feeds.update({f"adam_m_{n}": m[n] for n in PARAM_NAMES})
        feeds.update({f"adam_v_{n}": v[n] for n in PARAM_NAMES})
        feeds["adam_t"] = t
        feeds["input2"], feeds["input3"] = x, tt
        res = dict(zip(outs, sess.run(None, feeds)))
        w = {n: res[f"output_1_{n}"] for n in PARAM_NAMES}
        m = {n: res[f"output_2_m_{n}"] for n in PARAM_NAMES}
        v = {n: res[f"output_2_v_{n}"] for n in PARAM_NAMES}
        t = res["output_2_t"]
        log.append((i, float(res["output_0"]), accuracy(w) if i % EPOCH == EPOCH - 1 else None))
    return log, w


def run_torch() -> tuple[list, dict]:
    import torch

    w0 = load_init()
    adapters = {n: torch.from_numpy(w0[n]).requires_grad_(True) for n in ADAPTERS}
    frozen = {n: torch.from_numpy(w0[n]) for n in PARAM_NAMES if n not in ADAPTERS}
    opt = torch.optim.Adam([adapters[n] for n in ADAPTERS], lr=LR)  # betas (0.9, 0.999), eps 1e-8
    log = []
    for i in range(STEPS):
        x, t = batches(i)
        xt, tt = torch.from_numpy(x), torch.from_numpy(t)
        h = xt
        for n in ("fc1", "fc2", "fc3"):
            h = h @ frozen[f"{n}_w"] + (h @ adapters[f"{n}_lora_b"]) @ adapters[f"{n}_lora_a"] * 2.0 + frozen[f"{n}_b"]
            if n != "fc3":
                h = torch.relu(h)
        loss = torch.nn.functional.cross_entropy(h, tt.argmax(1))
        opt.zero_grad()
        loss.backward()
        opt.step()
        w = {**frozen, **{n: p.detach() for n, p in adapters.items()}}
        log.append((i, float(loss), accuracy({k: (v.numpy() if hasattr(v, "numpy") else v) for k, v in w.items()}) if i % EPOCH == EPOCH - 1 else None))
    return log, {n: (w[n].numpy() if hasattr(w[n], "numpy") else w[n]) for n in PARAM_NAMES}


def run_native() -> tuple[list, dict]:
    import os
    import re

    dump = ROOT / "lora" / "out" / "native_final_adam"
    env = camel_env(
        "train_ce_adam",
        {
            "CAMEL_FT_STEPS": str(STEPS),
            "CAMEL_FT_NBATCHES": str(NB),
            "CAMEL_FT_DUMP": str(dump),
        },
    )
    proc = subprocess.run(
        [str(ROOT.parent / "out" / "latest" / "bin" / "camel.exe"), str(ROOT / "lora" / "lora_mlp.cml")],
        capture_output=True,
        text=True,
        env=env,
        cwd=ROOT / "lora",
    )
    if proc.returncode != 0:
        raise RuntimeError(f"native run failed: {proc.stderr[-2000:]}")
    losses = [(int(m.group(1)), float(m.group(2))) for m in re.finditer(r"step=(\d+) loss=([0-9.eE+-]+)", proc.stdout)]
    w = {n: np.load(dump / f"{n}.npy") for n in PARAM_NAMES}
    log = [(i, l, accuracy(w) if i % EPOCH == EPOCH - 1 else None) for i, l in losses]
    return log, w


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    if "--no-export" not in sys.argv:
        export_graph()
    print(f"[ort] driving {GRAPH.name} for {STEPS} steps ...")
    ort_log, ort_w = run_ort()
    print("[torch] reference loop ...")
    torch_log, torch_w = run_torch()
    print("[camel-native] subprocess ...")
    nat_log, nat_w = run_native()

    with (OUT / "curves.csv").open("w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["step", "loss_ort", "loss_torch", "loss_native", "acc_ort", "acc_torch", "acc_native"])
        for (i, lo, ao), (_, lt, at), (_, ln, an) in zip(ort_log, torch_log, nat_log):
            wcsv.writerow([i, lo, lt, ln, ao, at, an])

    rows = []
    for i, ((_, lo, _), (_, lt, _), (_, ln, _)) in enumerate(zip(ort_log, torch_log, nat_log)):
        rows.append((i, abs(lo - ln), abs(lo - lt), abs(ln - lt)))
    with (OUT / "errors.csv").open("w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["step", "ort_vs_native", "ort_vs_torch", "native_vs_torch"])
        wcsv.writerows(rows)

    with (OUT / "summary.txt").open("w") as f:
        f.write(f"T3 three-way LoRA fine-tune, in-graph Adam (rank 8, lr {LR}, {STEPS} steps, user = 1024 rotated MNIST)\n")
        f.write(f"accuracy BEFORE: {accuracy(load_init()):.4f}\n")
        f.write(f"accuracy AFTER : ort {accuracy(ort_w):.4f}  torch {accuracy(torch_w):.4f}  native {accuracy(nat_w):.4f}\n")
        f.write(f"max|dloss| over {STEPS} steps: ort/native {max(r[1] for r in rows):.2e}  "
                f"ort/torch {max(r[2] for r in rows):.2e}  native/torch {max(r[3] for r in rows):.2e}\n")
        for n in ADAPTERS:
            e1 = float(np.max(np.abs(ort_w[n] - nat_w[n])))
            e2 = float(np.max(np.abs(ort_w[n] - torch_w[n])))
            e3 = float(np.max(np.abs(nat_w[n] - torch_w[n])))
            s = max(float(np.max(np.abs(torch_w[n]))), 1e-12)
            f.write(f"  {n:12s} final max|dW|/max|W|: ort/nat {e1 / s:.2e}  ort/tor {e2 / s:.2e}  nat/tor {e3 / s:.2e}\n")
    print((OUT / "summary.txt").read_text())
    print(f"[done] wrote {OUT}/curves.csv, errors.csv, summary.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
