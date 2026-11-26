"""Runs the exported `while`-collecting loop on ONNX Runtime and compares with Camel's array.

The model is `ort_while_collect.cml`: a `while` that collects one value per round into an array,
exported as an ONNX Loop with no trip-count input and a scan output. The condition -- not the
input's length -- decides how many values the array holds, so the two inputs below collect two
different counts (10 values and 4). Camel writes the array it computes to `y.npy` (int64) and the
counter it ended on to stdout; this runs the exported model on the same input and compares shape
and values, and reads the model's declared output shape for a stated length it must not have.

Usage: python ort_while_collect.py   (needs numpy and onnxruntime)
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
PROGRAM = HERE / "loop_collect" / "ort_while_collect.cml"


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
        # The condition's value is the sum of the input, so these two inputs of different lengths
        # ask the loop for a different number of rounds each; neither count is an input length.
        for x, rounds in (
            (np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32), 10),
            (np.array([2.0, 2.0], dtype=np.float32), 4),
        ):
            native = camel(x, tmp, model)
            session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
            array, counter = session.run(None, {session.get_inputs()[0].name: x})
            declared = session.get_outputs()[0].shape
            # The length is the rounds the run took, so the model must not state one: a number
            # here would be the lie that made this shape refuse to export in the first place.
            unstated = not isinstance(declared[0], int)
            same = native.shape == array.shape and np.array_equal(native, array)
            counted = native.shape == (rounds,) and int(counter) == rounds
            print(
                f"sum={int(x.sum())} camel {list(native.shape)} {native.tolist()} "
                f"ort {list(array.shape)} {array.tolist()} declared {declared} "
                f"counter {int(counter)} "
                f"{'ok' if same and counted and unstated else 'MISMATCH'}"
            )
            ok &= same and counted and unstated
    print("ort loop collect while:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())