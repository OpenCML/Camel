"""E14/T7: one dynamic-batch TRAINING step graph vs per-batch specialized graphs.

The value_and_grad + sgd step of ort_train_dynamic.cml (batch mean written as
sum / shape arithmetic) is exported once with dynamic_axes = [0] and once per
batch size (1, 16, 64) as a specialized static graph. ONNX Runtime runs both at
each batch size; the loss and every updated parameter must match within 1e-6,
and the dynamic graph must also match Camel's own native run of the same step.

Usage: python ort_train_dynamic.py   (needs numpy and onnxruntime)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PROGRAM = HERE / "ort_train_dynamic.cml"
PARAMS = ["w1", "b1", "w2", "b2"]
BATCHES = [1, 16, 64]
EXPORT_BATCH = 4
TOL = 1e-6

W0 = {
    "w1": np.array([[0.5, -0.25, 0.75], [1.0, -0.5, 0.25]], dtype=np.float32),
    "b1": np.array([0.1, 0.0, -0.1], dtype=np.float32),
    "w2": np.array([[1.0], [-1.0], [0.5]], dtype=np.float32),
    "b2": np.array([0.2], dtype=np.float32),
}


def camel(x: np.ndarray, export: Path | None, dyn: bool, dump: Path, tmp: Path) -> float:
    xin = tmp / "x.npy"
    np.save(xin, x)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {
        **os.environ,
        "X": str(xin),
        "ONNX_OUT": str(export or ""),
        "DYN": "1" if dyn else "0",
        "DUMP": str(dump),
    }
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")
    return float(proc.stdout.strip().splitlines()[-1].split("loss=")[1])


def ort_run(path: Path, x: np.ndarray) -> dict:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    outs = [o.name for o in sess.get_outputs()]
    return dict(zip(outs, sess.run(None, {**W0, "input1": x})))


def main() -> int:
    ok = True
    worst = 0.0
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        rng = np.random.default_rng(7)
        xs = {b: rng.random((b, 2), dtype=np.float32) for b in sorted({*BATCHES, EXPORT_BATCH})}
        dyn = tmp / "train_step_dynbatch.onnx"
        camel(xs[EXPORT_BATCH], dyn, True, tmp / "dump_export", tmp)
        dims = __import__("onnxruntime").InferenceSession(str(dyn)).get_inputs()[-1].shape
        print(f"input dims {dims}")
        for b in BATCHES:
            spec = tmp / f"train_step_b{b}.onnx"
            dump = tmp / f"dump_b{b}"
            nat_loss = camel(xs[b], spec, False, dump, tmp)
            got, ref = ort_run(dyn, xs[b]), ort_run(spec, xs[b])
            pairs = [("loss", ref["output_0"], got["output_0"], np.float32(nat_loss))]
            for n in PARAMS:
                pairs.append((n, ref[f"output_1_{n}"], got[f"output_1_{n}"], np.load(dump / f"{n}.npy")))
            for name, spec_v, dyn_v, nat_v in pairs:
                e1 = float(np.max(np.abs(dyn_v - spec_v)))
                e2 = float(np.max(np.abs(dyn_v - nat_v)))
                worst = max(worst, e1, e2)
                ok &= e1 <= TOL and e2 <= TOL
                print(f"batch {b:3d} {name:5s} dyn-vs-spec {e1:.2e}  ort-vs-native {e2:.2e}")
    print(f"ort train dynamic: worst {worst:.2e}", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
