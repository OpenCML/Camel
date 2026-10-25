"""The core's own slice exported to ONNX: an in-range run gives the NumPy slice, and every run
Camel faults on must make ONNX Runtime fail instead of clamping.

`ort_core_slice.cml` builds `:op/slice_sub`, the node the `[]` forms produce for any rank and any
axis (design principles 5.4); the tensor module is not on that path. An ONNX Slice clamps a bound
that leaves the axis, where Camel faults (design principles 3.9), so a bound the exporter cannot
prove in range goes through a Gather guard. The rows below separate that guard from a clamp: they
run each model with an axis shorter than the bound says, and one of them feeds a start the data
makes negative, which a plain ONNX Slice would read as a position from the end of the axis.

Usage: python ort_core_slice.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_core_slice.cml"


def reshape_rows(values: np.ndarray) -> np.ndarray:
    """The reshape `rows` in the program makes: the data says how many rows axis 0 has."""
    rows = int(values.sum())
    return values.reshape(rows, int(4.0 / values.sum()))


# mode, the model input, and the slice of it the run must produce (None: Camel faults there)
CASES = [
    ("both", [1.0, 1.0, 1.0, 1.0], lambda x: reshape_rows(x)[1:3]),
    ("both", [1.0, 1.0, 0.0, 0.0], None),  # the axis has 2 rows, the bound says 3
    ("both", [1.0, 0.0, 0.0, 0.0], None),  # the axis has 1 row
    ("open", [1.0, 1.0, 1.0, 1.0], lambda x: reshape_rows(x)[2:]),
    ("open", [1.0, 1.0, 0.0, 0.0], lambda x: reshape_rows(x)[2:]),  # empty: start = the extent
    ("open", [1.0, 0.0, 0.0, 0.0], None),  # start 2 leaves an axis of 1
    ("start", [1.0, 1.0, 1.0, 1.0], lambda x: x[4:]),  # the start is the extent: empty
    ("start", [1.0, 0.0, 0.0, 0.0], lambda x: x[1:]),
    ("start", [0.0, 0.0, 0.0, 0.0], lambda x: x[:]),
    ("start", [-1.0, -1.0, -1.0, 1.0], None),  # a negative start: Camel faults, ONNX would count
    ("rank2", [1.0, 2.0, 3.0, 4.0], lambda x: x.reshape(2, 2)[1:2]),
    ("known", [1.0, 2.0, 3.0, 4.0], lambda x: x[1:3]),
    ("step", [1.0, 2.0, 3.0, 4.0], lambda x: x[0:4:2]),
]


def main() -> int:
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        for mode in sorted({row[0] for row in CASES}):
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
            for rowMode, values, expected in CASES:
                if rowMode != mode:
                    continue
                x = np.array(values, np.float32)
                want = None if expected is None else np.asarray(expected(x), np.float32)
                try:
                    got = session.run(None, {name: x})[0]
                except Exception:
                    got = None
                if want is None:
                    ok = got is None
                    what = "fails" if ok else f"returned {got} where Camel faults"
                else:
                    ok = got is not None and np.array_equal(got, want)
                    what = f"{got}"
                print(f"{mode} {values}: {what} {'ok' if ok else 'FAIL'}")
                failures += 0 if ok else 1
    print(f"ort core slice: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())