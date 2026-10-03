"""Three-way numeric alignment of the in-graph Adam export (T3).

Five Adam steps of the tiny MLP in export_train_step_adam.cml:
  ort     the exported graph driven by an unmodified ONNX Runtime session,
          with (m, v, t) fed back as graph inputs each step
  torch   the same MLP updated by torch.optim.Adam (default betas/eps)
  native  Camel's own nn:adam (export_train_step_adam_dump.cml)

Compares the loss and all updated parameters per step; tolerance 1e-5.
Usage: python ort_adam.py   (needs numpy, onnxruntime, torch)
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
EXPORT_CML = HERE / "export_train_step_adam.cml"
DUMP_CML = HERE / "export_train_step_adam_dump.cml"
STEPS, LR = 5, 1e-3
PARAMS = ["w1", "b1", "w2", "b2"]

M0 = {
    "w1": np.array([[0.5, -0.25, 0.75], [1.0, -0.5, 0.25]], dtype=np.float32),
    "b1": np.array([0.1, 0.0, -0.1], dtype=np.float32),
    "w2": np.array([[1.0], [-1.0], [0.5]], dtype=np.float32),
    "b2": np.array([0.2], dtype=np.float32),
}
X = np.array([[1.0, 2.0], [-1.0, 0.5]], dtype=np.float32)


def camel(cml: Path, env_extra: dict) -> subprocess.CompletedProcess:
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, **env_extra}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(cml)], env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")
    return proc


def run_ort(graph: Path) -> list:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(graph), providers=["CPUExecutionProvider"])
    outs = [o.name for o in sess.get_outputs()]
    w, t = dict(M0), np.array(0, dtype=np.int64)
    m = {n: np.zeros_like(M0[n]) for n in PARAMS}
    v = {n: np.zeros_like(M0[n]) for n in PARAMS}
    log = []
    for _ in range(STEPS):
        feeds = {**w, "input2": X, "adam_t": t}
        feeds.update({f"adam_m_{n}": m[n] for n in PARAMS})
        feeds.update({f"adam_v_{n}": v[n] for n in PARAMS})
        res = dict(zip(outs, sess.run(None, feeds)))
        w = {n: res[f"output_1_{n}"] for n in PARAMS}
        m = {n: res[f"output_2_m_{n}"] for n in PARAMS}
        v = {n: res[f"output_2_v_{n}"] for n in PARAMS}
        t = res["output_2_t"]
        log.append((float(res["output_0"]), w))
    return log


def run_torch() -> list:
    import torch

    w = {n: torch.from_numpy(M0[n]).requires_grad_(True) for n in PARAMS}
    opt = torch.optim.Adam([w[n] for n in PARAMS], lr=LR)  # betas (0.9, 0.999), eps 1e-8
    x = torch.from_numpy(X)
    log = []
    for _ in range(STEPS):
        h = torch.relu(x @ w["w1"] + w["b1"])
        y = h @ w["w2"] + w["b2"]
        loss = (y * y).mean()
        opt.zero_grad()
        loss.backward()
        opt.step()
        log.append((float(loss.detach()), {n: w[n].detach().numpy().copy() for n in PARAMS}))
    return log


def run_native(dump: Path) -> list:
    proc = camel(DUMP_CML, {"STEPS": str(STEPS), "DUMP_DIR": str(dump)})
    losses = [float(m.group(2)) for m in re.finditer(r"step=(\d+) loss=([0-9.eE+-]+)", proc.stdout)]
    return [
        (losses[i], {n: np.load(dump / f"step_{i}" / f"{n}.npy") for n in PARAMS})
        for i in range(STEPS)
    ]


def main() -> int:
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        graph = tmp / "step_adam.onnx"
        camel(EXPORT_CML, {"ONNX_OUT": str(graph)})
        parties = {"ort": run_ort(graph), "torch": run_torch(), "native": run_native(tmp / "nat")}
    worst = 0.0
    for i in range(STEPS):
        for a, b in (("ort", "torch"), ("ort", "native"), ("torch", "native")):
            dl = abs(parties[a][i][0] - parties[b][i][0])
            dw = max(
                float(np.max(np.abs(parties[a][i][1][n] - parties[b][i][1][n]))) for n in PARAMS
            )
            worst = max(worst, dl, dw)
            print(f"step {i} {a}/{b}: |dloss| {dl:.2e} max|dW| {dw:.2e}")
    ok = worst <= 1e-5
    print(f"ort adam: worst {worst:.2e}", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
