"""A slice with a model-dependent end exported to ONNX faults on ONNX Runtime where Camel faults.

ort_slice_bounds.cml slices x[0 : int(sum(x))]. Camel faults when the end is outside [-n, n]
(design principles 3.9); the ONNX Slice alone would clamp it and return a result. Each model
(the slice alone, and inside a branch on the data) is run on ONNX Runtime: in-range ends give
the NumPy slice, out-of-range ends must make the run fail.

Usage: python ort_slice_bounds.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_slice_bounds.cml"

CASES = [
    ([1.0, 1.0, 1.0], 3),        # end 3 = n: the whole axis
    ([0.5, 0.5, 0.5], 1),        # end 1
    ([-0.5, -0.5, -1.0], -2),    # end -2 counts from the end: 1
    ([-1.0, -1.0, -1.0], -3),    # end -3 = -n: empty
    ([2.0, 2.0, 2.0], None),     # end 6 > n: Camel faults
    ([-2.0, -2.0, -1.0], None),  # end -5 < -n: Camel faults
    ([1.5, 1.5, 1.0], None),     # end 4 = n + 1: Camel faults
]


def main() -> int:
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        for mode in ("head", "branch"):
            model = Path(t) / f"{mode}.onnx"
            env = {**os.environ, "ONNX_OUT": str(model), "MODE": mode}
            env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
            proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
            if proc.returncode != 0:
                print(f"{mode}: export failed: {(proc.stdout + proc.stderr)[-300:]}")
                failures += 1
                continue
            session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
            name = session.get_inputs()[0].name
            for values, end in CASES:
                x = np.array(values, np.float32)
                try:
                    got = session.run(None, {name: x})[0]
                except Exception:
                    got = None
                if end is None:
                    ok = got is None
                    what = "fails" if ok else f"returned {got} where Camel faults"
                else:
                    ok = got is not None and np.array_equal(got, x[:end])
                    what = f"{got}"
                print(f"{mode} {values}: {what} {'ok' if ok else 'FAIL'}")
                failures += 0 if ok else 1
    print(f"ort slice bounds: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
