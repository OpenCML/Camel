"""A slice whose start depends on the model input faults on ONNX Runtime where Camel faults.

ort_slice_start.cml slices x[int(sum(x)) : 3] on an axis of extent 3. A bound is a position of
the axis: the accepted values are 0 to 3, where 3 is the end and takes nothing, and a negative
bound is a fault rather than a count from the end (design principles 3.9, the core's reading of
its own slice node). ONNX Slice alone clamps out-of-range bounds, so the exporter guards the
interval it reads from the operator's index semantics declaration. This script checks every
value at and just outside both edges, which separates the rule from an off-by-one interval or a
clamped one, and checks the constant-bound path (where the declaration proves the bounds in
range, so the bounds the program wrote go to a plain ONNX Slice).

Usage: python ort_slice_start.py   (needs numpy, onnx and onnxruntime)
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
PROGRAM = HERE / "ort_slice_start.cml"

# (input, what the start becomes). n = 3, end = 3: the result is x[start:3], a position from 0
# to 3. None: the start leaves [0, 3] -- a negative one included -- and Camel faults.
START_CASES = [
    ([3.0, 0.0, 0.0], 3),     # start = n: empty range at the end, not a fault
    ([1.0, 0.0, 0.0], 1),     # start = 1
    ([0.0, 0.0, 0.0], 0),     # start = 0
    ([-2.0, 0.0, 0.0], None),  # start = -2: a negative bound is a fault, not 1 from the end
    ([-3.0, 0.0, 0.0], None),  # start = -3: the same, not the whole axis
    ([-4.0, 0.0, 0.0], None),  # start = -4 = -n - 1: Camel faults
    ([4.0, 0.0, 0.0], None),   # start = n + 1: Camel faults
    ([-5.0, 0.0, 0.0], None),  # start = -5: Camel faults
]


def main() -> int:
    import onnxruntime as ort

    ort.set_default_logger_severity(4)
    exe = REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel")
    failures = 0
    with tempfile.TemporaryDirectory() as t:
        model = Path(t) / "start.onnx"
        env = {**os.environ, "ONNX_OUT": str(model), "MODE": "start"}
        env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
        proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"start: export failed: {(proc.stdout + proc.stderr)[-400:]}")
            return 1
        session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        name = session.get_inputs()[0].name
        for values, start in START_CASES:
            x = np.array(values, np.float32)
            try:
                got = session.run(None, {name: x})[0]
            except Exception:
                got = None
            if start is None:
                ok = got is None
                what = "fails" if ok else f"returned {got.tolist()} where Camel faults"
            else:
                want = x[start:3]
                ok = got is not None and np.array_equal(got, want)
                what = f"{None if got is None else got.tolist()}"
            print(f"start {values[0]}: {what} {'ok' if ok else 'FAIL'}")
            failures += 0 if ok else 1

        model = Path(t) / "constant.onnx"
        env = {**os.environ, "ONNX_OUT": str(model), "MODE": "constant"}
        proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"constant: export failed: {(proc.stdout + proc.stderr)[-400:]}")
            return 1
        session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        x = np.array([1.0, 2.0, 3.0], np.float32)
        got = session.run(None, {session.get_inputs()[0].name: x})[0]
        ok = np.array_equal(got, x[1:3])
        print(f"constant 1: {got.tolist()} {'ok' if ok else 'FAIL'}")
        failures += 0 if ok else 1

        # Constant bounds on an axis whose extent the model only knows at run time: the extent
        # is int(sum(x)) and the reshape target keeps the element count at 12, so sums 12, 6, 4
        # and 3 leave the axis long enough for [1 : 3] and sums 2 and 1 do not (Camel faults).
        model = Path(t) / "dynamic.onnx"
        env = {**os.environ, "ONNX_OUT": str(model), "MODE": "dynamic"}
        proc = subprocess.run([str(exe), str(PROGRAM)], env=env, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"dynamic: export failed: {(proc.stdout + proc.stderr)[-400:]}")
            return 1
        session = ort.InferenceSession(str(model), providers=["CPUExecutionProvider"])
        name = session.get_inputs()[0].name
        for total, fits in [(12, True), (6, True), (4, True), (3, True), (2, False), (1, False)]:
            x = np.zeros((1, 12), np.float32)
            x[0, 0] = total - 2
            x[0, 1] = 1.0
            x[0, 2] = 1.0
            try:
                got = session.run(None, {name: x})[0]
            except Exception:
                got = None
            if fits:
                want = x.reshape(12 // total, total)[:, 1:3]
                ok = got is not None and np.array_equal(got, want)
                what = f"{None if got is None else got.tolist()}"
            else:
                ok = got is None
                what = "fails" if ok else f"returned {got.tolist()} where Camel faults"
            print(f"dynamic extent {total}: {what} {'ok' if ok else 'FAIL'}")
            failures += 0 if ok else 1
    print(f"ort slice start: {failures} failing")
    if failures == 0:
        print("PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())