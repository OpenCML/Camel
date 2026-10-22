# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E14/T7 probe driver: dynamic-batch TRAINING step export (with_grad + sgd).

One graph exported with dynamic_axes = [0] (benchmarks/e14dyn/dyn_train_step.cml,
LOSS=summean: batch mean written as sum / shape arithmetic) is run on ONNX
Runtime at batch sizes 1, 16 and 64 and compared, per batch, against

  * a per-batch SPECIALIZED export (same step, static batch), and
  * Camel's own native run of the same step (artifact-correctness gate).

The canonical LOSS=mean form (mean(y*y) -> tensor:numel in the VJP) is probed
too: its dynamic-batch export failure is recorded verbatim as the gap evidence.

Outputs (benchmarks/e14dyn/out/):
  dynamic_vs_specialized.csv   batch, output, max|diff| dynamic-vs-specialized
  ort_vs_native.csv            batch, output, max|diff| ORT(dynamic)-vs-native
  mean_failure.log             verbatim compiler error for LOSS=mean + DYN=1
  summary.txt                  headline numbers
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
OUT = HERE / "out"
PROGRAM = HERE / "dyn_train_step.cml"
EXE = REPO / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
PARAMS = ["w1", "b1", "w2", "b2"]
BATCHES = [1, 16, 64]
EXPORT_BATCH = 4  # the example batch the dynamic graph is exported with
TOL = 1e-6

W0 = {
    "w1": np.array([[0.5, -0.25, 0.75], [1.0, -0.5, 0.25]], dtype=np.float32),
    "b1": np.array([0.1, 0.0, -0.1], dtype=np.float32),
    "w2": np.array([[1.0], [-1.0], [0.5]], dtype=np.float32),
    "b2": np.array([0.2], dtype=np.float32),
}


def camel_run(x: np.ndarray, export: Path | None, dyn: bool, loss: str, dump: Path) -> float:
    xin = OUT / "x_in.npy"
    np.save(xin, x)
    env = {
        **os.environ,
        "X": str(xin),
        "ONNX_OUT": str(export or ""),
        "DYN": "1" if dyn else "0",
        "LOSS": loss,
        "DUMP": str(dump),
    }
    env.setdefault("CAMEL_HOME", str(REPO / "out" / "latest"))
    proc = subprocess.run([str(EXE), str(PROGRAM)], env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"camel failed (dyn={dyn} loss={loss}):\n{proc.stdout}\n{proc.stderr}")
    line = proc.stdout.strip().splitlines()[-1]
    return float(line.split("loss=")[1])


def camel_mean_failure() -> str:
    xin = OUT / "x_in.npy"
    env = {
        **os.environ,
        "X": str(xin),
        "ONNX_OUT": str(OUT / "mean_dyn.onnx"),
        "DYN": "1",
        "LOSS": "mean",
        "DUMP": str(OUT / "dump_mean"),
    }
    env.setdefault("CAMEL_HOME", str(REPO / "out" / "latest"))
    proc = subprocess.run([str(EXE), str(PROGRAM)], env=env, capture_output=True, text=True)
    return f"exit={proc.returncode}\n--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}"


def ort_run(path: Path, x: np.ndarray) -> dict:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    feeds = {**W0, "input1": x}
    outs = [o.name for o in sess.get_outputs()]
    return dict(zip(outs, sess.run(None, feeds)))


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(7)
    xs = {b: rng.random((b, 2), dtype=np.float32) for b in sorted({*BATCHES, EXPORT_BATCH})}

    print(f"[export] dynamic graph (example batch {EXPORT_BATCH}) ...")
    dyn = OUT / "train_step_dynbatch.onnx"
    camel_run(xs[EXPORT_BATCH], dyn, True, "summean", OUT / "dump_export")

    spec = {}
    native = {}
    for b in BATCHES:
        print(f"[export+native] specialized batch {b} ...")
        spec[b] = OUT / f"train_step_b{b}.onnx"
        dump = OUT / f"dump_native_b{b}"
        native[b] = (camel_run(xs[b], spec[b], False, "summean", dump), dump)

    rows_dyn, rows_nat = [], []
    ok = True
    for b in BATCHES:
        got = ort_run(dyn, xs[b])
        ref = ort_run(spec[b], xs[b])
        nat_loss, nat_dump = native[b]
        nat_params = {n: np.load(nat_dump / f"{n}.npy") for n in PARAMS}
        pairs = [("loss", ref["output_0"], got["output_0"], np.array(nat_loss, dtype=np.float32), got["output_0"])]
        for n in PARAMS:
            pairs.append((n, ref[f"output_1_{n}"], got[f"output_1_{n}"], nat_params[n], got[f"output_1_{n}"]))
        for name, spec_v, dyn_v, nat_v, got_v in pairs:
            e1 = float(np.max(np.abs(dyn_v - spec_v)))
            e2 = float(np.max(np.abs(got_v - nat_v)))
            rows_dyn.append((b, name, e1))
            rows_nat.append((b, name, e2))
            flag = "ok" if e1 <= TOL else "MISMATCH"
            flag2 = "ok" if e2 <= TOL else "MISMATCH"
            ok &= e1 <= TOL and e2 <= TOL
            print(f"batch {b:3d} {name:5s} dyn-vs-spec {e1:.2e} {flag}   ort-vs-native {e2:.2e} {flag2}")

    with (OUT / "dynamic_vs_specialized.csv").open("w") as f:
        f.write("batch,output,max_abs_diff\n")
        f.writelines(f"{b},{n},{e:.3e}\n" for b, n, e in rows_dyn)
    with (OUT / "ort_vs_native.csv").open("w") as f:
        f.write("batch,output,max_abs_diff\n")
        f.writelines(f"{b},{n},{e:.3e}\n" for b, n, e in rows_nat)

    failure = camel_mean_failure()
    (OUT / "mean_failure.log").write_text(failure)
    print(f"[gap] LOSS=mean dynamic export -> {failure.splitlines()[0]} (verbatim in mean_failure.log)")

    worst_dyn = max(e for _, _, e in rows_dyn)
    worst_nat = max(e for _, _, e in rows_nat)
    with (OUT / "summary.txt").open("w") as f:
        f.write("E14/T7 dynamic-batch training step (2-layer MLP, with_grad + sgd, lr 0.01)\n")
        f.write(f"loss: batch mean as sum/shape arithmetic (LOSS=summean); canonical mean(y*y) "
                f"dynamic export FAILS -> tensor:numel has no ONNX lowering (mean_failure.log)\n")
        f.write(f"dynamic graph exported with example batch {EXPORT_BATCH}, run at batches {BATCHES}\n")
        f.write(f"worst max|diff| dynamic-vs-specialized: {worst_dyn:.3e} (tol {TOL:.0e})\n")
        f.write(f"worst max|diff| ORT(dynamic)-vs-native: {worst_nat:.3e} (tol {TOL:.0e})\n")
        f.write(f"verdict: {'PASS' if ok else 'FAIL'}\n")
    print((OUT / "summary.txt").read_text())
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
