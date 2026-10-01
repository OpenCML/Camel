"""Runs exported Camel models on ONNX Runtime and compares with Camel's own output.

  branch   `gate` exports as an ONNX If; inputs taking each arm give the same
           result on ONNX Runtime as in Camel.
  dynamic  `forward` exports with a dynamic batch axis; one model runs on
           batches of several sizes and matches Camel on each.

Usage: python ort_run.py branch|dynamic   (needs numpy and onnxruntime)
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import onnxruntime as ort

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
PROGRAM = HERE / "ort_models.cml"


def camel(model: str, x: np.ndarray, tmp: Path, export: Path | None = None) -> np.ndarray:
    xin, yout = tmp / "x.npy", tmp / "y.npy"
    np.save(xin, x)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "MODEL": model, "X": str(xin), "Y": str(yout), "ONNX_OUT": str(export or "")}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")
    return np.load(yout)


def ort_run(path: Path, x: np.ndarray) -> np.ndarray:
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    return session.run(None, {session.get_inputs()[0].name: x})[0]


def compare(label: str, ref: np.ndarray, got: np.ndarray) -> bool:
    err = float(np.max(np.abs(ref - got))) if ref.shape == got.shape else float("inf")
    ok = err <= 1e-5
    print(f"{label}: shape {list(got.shape)} max|diff| {err:.2e} {'ok' if ok else 'MISMATCH'}")
    return ok


def main() -> int:
    mode = sys.argv[1]
    ok = True
    with tempfile.TemporaryDirectory() as t:
        tmp, model = Path(t), Path(t) / "model.onnx"
        if mode == "branch":
            then_x = np.array([1.0, -2.0, 3.0, 0.5], dtype=np.float32)  # t[0] > 0.5
            else_x = np.array([0.25, -2.0, 3.0, 0.5], dtype=np.float32)
            camel("gate", then_x, tmp, export=model)
            for label, x in (("then arm", then_x), ("else arm", else_x)):
                ok &= compare(label, camel("gate", x, tmp), ort_run(model, x))
        elif mode == "dynamic":
            rng = np.random.default_rng(0)
            camel("forward", rng.random((2, 2), dtype=np.float32), tmp, export=model)
            dims = ort.InferenceSession(str(model)).get_inputs()[0].shape
            print(f"input dims {dims}")
            for batch in (1, 3, 7):
                x = rng.random((batch, 2), dtype=np.float32)
                ok &= compare(f"batch {batch}", camel("forward", x, tmp), ort_run(model, x))
        else:
            raise SystemExit(f"unknown mode {mode}")
    print("ort run:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
