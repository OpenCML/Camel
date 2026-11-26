"""Runs the exported collecting loop on ONNX Runtime and compares with Camel's own array.

The model is `ort_collect.cml`: a counted `for` that collects one value per round into an array,
exported as an ONNX Loop whose scan output is that array. Camel writes the array it computes to
`y.npy` (int64); this runs the exported model on the same input and compares shape and values,
so the model's declared output shape and the array the interpreter builds are read against each
other on one program.

Usage: python ort_loop_collect.py   (needs numpy and onnxruntime)
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
PROGRAM = HERE / "loop_collect" / "ort_collect.cml"


def camel(x: np.ndarray, tmp: Path, export: Path) -> np.ndarray:
    np.save(tmp / "x.npy", x)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    env = {**os.environ, "ONNX_OUT": str(export)}
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run(
        [str(exe), str(PROGRAM)], env=env, cwd=tmp, capture_output=True, text=True
    )
    if proc.returncode != 0:
        raise SystemExit(f"camel failed:\n{proc.stdout}\n{proc.stderr}")
    return np.load(tmp / "y.npy")


def main() -> int:
    ok = True
    with tempfile.TemporaryDirectory() as t:
        tmp, model = Path(t), Path(t) / "model.onnx"
        # A short batch and a longer one: the trip count is the length of the input, so the
        # exported Loop must collect the number of rounds the data asks for, not a fixed one.
        for x in (
            np.array([0.1, 0.2, 0.3, 0.4], dtype=np.float32),
            np.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0], dtype=np.float32),
        ):
            native = camel(x, tmp, model)
            session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
            got = session.run(None, {session.get_inputs()[0].name: x})[0]
            declared = session.get_outputs()[0].shape
            same = native.shape == got.shape and np.array_equal(native, got)
            print(
                f"n={len(x)} camel {list(native.shape)} {native.tolist()} "
                f"ort {list(got.shape)} {got.tolist()} declared {declared} "
                f"{'ok' if same else 'MISMATCH'}"
            )
            ok &= same
    print("ort loop collect:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())