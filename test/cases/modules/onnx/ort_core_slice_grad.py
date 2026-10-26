"""The reverse of the core's slice on ONNX Runtime: the exported scatter writes the gradient into
the positions the slice took and leaves the rest of the array zero.

`ort_core_slice_grad.cml` builds `:tangent/scatter_slice_arr`, the adjoint of the core's own
`:op/slice_sub` (design principles 5.4), and exports the training step of a loss that slices. The
expected gradients below are the analytic ones -- `d loss / d x = w` at the sliced positions and
zero elsewhere, `d loss / d w = x` there -- so the comparison does not go through Camel's own
backward pass at all.

Usage: python ort_core_slice_grad.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_core_slice_grad.cml"


def expected(w: np.ndarray, x: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """What the loss and `d loss / d x` are for `sum(w * x[1:3])`."""
    loss = np.array([np.float32(np.sum(w[1:3] * x[1:3]))], np.float32)
    grad = np.zeros_like(x)
    grad[1:3] = w[1:3]
    return loss, grad


# The program's own (w, x), a second pair, and one whose sliced weights are zero.
CASES = [
    ([0.5, 0.25, -1.0, 2.0], [1.0, 2.0, 3.0, 4.0]),
    ([3.0, -2.0, 0.5, 7.0], [0.5, 1.5, -2.5, 4.0]),
    ([1.0, 0.0, 0.0, 1.0], [2.0, 3.0, 4.0, 5.0]),
]


def main() -> int:
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        model = Path(t) / "core_slice_grad.onnx"
        env = {**os.environ, "ONNX_OUT": str(model)}
        env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
        proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"export failed: {(proc.stdout + proc.stderr)[-400:]}")
            print("ort core slice grad: 1 failing")
            return 1
        session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        names = [entry.name for entry in session.get_inputs()]
        for wValues, xValues in CASES:
            w = np.array(wValues, np.float32)
            x = np.array(xValues, np.float32)
            wantLoss, wantGrad = expected(w, x)
            gotLoss, gotGrad = session.run(None, {names[0]: w, names[1]: x})
            ok = np.allclose(gotLoss, wantLoss, atol=1e-6) and np.allclose(gotGrad, wantGrad, atol=1e-6)
            print(f"w={wValues} x={xValues}: loss={gotLoss} grad={gotGrad} {'ok' if ok else 'FAIL'}")
            failures += 0 if ok else 1
    print(f"ort core slice grad: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())